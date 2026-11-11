# Chain state snapshot (fast start)

A new installation can start from the state of the chain at a fixed height instead of
validating every block from the first one. The state is downloaded as a *snapshot*; the
program accepts it only when its content has the hashes that are compiled into the program.
After that the node validates the blocks that follow, like any other node.

This document says what is trusted, what a snapshot node can and cannot do, how the past
blocks are fetched later, how a snapshot is made, and what was measured.

Code: `src/snapshot.{h,cpp}`, `CChainParams::Snapshot()` in `src/chainparams.cpp`,
the hooks in `src/init.cpp` (step 7), `src/validation.cpp`, `src/net_processing.cpp`,
RPCs in `src/rpc/blockchain.cpp`, GUI in `src/qt/pastblocks.{h,cpp}`.
Tool: `contrib/aokchain/make_snapshot.py`. Test: `test/functional/feature_snapshot.py`.

## 1. Trust model

The block archives of [12-bootstrap.md](12-bootstrap.md) need no trust: every block is
validated. A snapshot is different. Its content cannot be validated without the blocks, so
the question is who vouches for it.

**The program does, not the server.** `CChainParams::Snapshot()` holds, for the main network:

| value | covers |
|---|---|
| `height`, `block` | the height of the snapshot and the hash of the block at that height |
| `utxo` | every unspent output with everything the chainstate stores for it: outpoint, height, coinbase mark, coinstake mark, time of the transaction, value, script |
| `tokens` | every record of the token database except the undo records of blocks (which a node deletes on accepting a snapshot) |
| `governance` | every record of the governance database |
| `index` | for every block of the chain: hash, height, version, merkle root, time, bits, nonce, stake modifier, number of transactions, status flags |

After unpacking, the node opens the databases and computes these hashes **from what it
loaded** (`ComputeStateHashes()`, the same code as the RPC `getstatehashes`). Any
difference, any error while loading, any record in the block index database that is not a
block index entry, a block file record or a flag: the snapshot is removed completely and
the node synchronizes from the blocks, as it would without a snapshot. The SHA-256 of the
files in `snapshot.json` only detects a damaged download; it is not what makes a snapshot
acceptable.

So a compromised download server cannot make a node accept other balances, tokens,
governance values or stake modifiers. It can make the snapshot unusable, which costs the
node the time of an ordinary synchronization.

What a user trusts with a snapshot is therefore **the program**: that the people who built
it computed these values from an honestly validated chain. That is the same kind of trust
as for the `assumevalid` block and the checkpoints, with a larger consequence: an
`assumevalid` block skips signature checks, a snapshot skips everything up to its height.
Whoever does not want that starts with `-snapshot=0` and validates every block; the hashes
that `getstatehashes` then prints at the snapshot height are the pinned ones
(`feature_snapshot.py` checks exactly this on regtest, and §7 says what was checked on the
main network).

### What the hashes do not cover

* The **positions of the blocks in the block files** and the records of the block files
  (in the block index database). They are checked for plausibility (ascending, inside the
  last file) and every past block that is stored later must fit between its neighbours. A
  wrong value can make the download of the past blocks fail or waste disk space; it cannot
  change the state.
* Nothing else. The snapshot has no transaction index (the node builds it for the blocks it
  connects, and for the past blocks when they are fetched) and no other index.

A snapshot that makes the program stop while loading (a damaged database) is tried once
more at the next start and removed after the second failure; the marker file
`snapshot-unverified` in the data directory keeps count.

## 2. What happens at the first start

1. `init.cpp` step 7, before the databases are opened: the data directory is new (or holds
   only the genesis block), a snapshot is pinned for the network, `-snapshot` and
   `-bootstrap` are on, no `-reindex`, `-prune` or non-default index option is given, and
   there is a source (`-bootstrapurl`, `-bootstrapdir`, or the built-in address in the GUI).
2. `snapshot.json` is read. A snapshot for another height or block is not downloaded.
3. The parts are downloaded (resumed after an interruption; mirrors as for the block
   archives) and unpacked into `chainstate/`, `blocks/index/`, `tokens/`, `governance/`.
   Only files directly in these four directories are accepted.
4. The databases are loaded, the hashes computed and compared (§1).
5. Accepted: the snapshot height is recorded in the block index database. The node is at
   that height and gets the following blocks from its peers.

The splash screen and `aokchain-cli` (while the RPC interface is warming up) show the
stage: looking for the snapshot, downloading (with the size and the time left), unpacking,
checking.

The daemon fetches `http://` only (see 12-bootstrap.md): on the main network it uses the
snapshot with `-bootstrapurl=<http mirror>` or `-bootstrapdir=<folder>`; the GUI also uses
the built-in `https://` address.

## 3. A snapshot node and the past blocks

A node that started from a snapshot at height H has no block files and no undo data for
the blocks up to H (the *past blocks*). This is what was established about what needs them.

**Validating new blocks: not needed.**
* The proof-of-stake checks (`pos.cpp`) read the unspent output that is staked and the
  stake modifier of the previous block, both of which are in the snapshot.
* `ConnectBlock()` used to read the staked transaction from disk through the transaction
  index, only to get its time. The coin has the same value (`AddCoins()` stores `tx.nTime`),
  so the coin is used now, on every node (see §6, and the consensus notes in §8).
* Token and governance checks read their databases.
* `CheckBlockStakeBeforeStorage()` (the check before a block is written to disk) uses the
  coin too. One case is left unchecked on a snapshot node without the past blocks: a block
  on a *fork* whose staked coin is from before the snapshot and has already been spent on
  the active chain. Such a block is stored and rejected when it would be connected, as
  before that check existed.

**Reorganizations.** Blocks above H are disconnected and connected as usual (the node has
their undo data). A fork that begins at or below H is refused
(`bad-fork-prior-to-snapshot`): the node cannot disconnect block H. Once the chain is more
than the maximum reorganization depth (500 blocks on the main network) past H this is no
restriction, because such a fork is refused by every node.

The undo data of a block keeps neither the time nor the coinstake mark of a spent coin
(an existing property: an ordinary node gives such a coin back with time 0 and reads the
transaction from disk when it is staked again). A snapshot node cannot read a transaction
of the past blocks, so it keeps time and mark of every coin from before the snapshot that
it spends (block index database, key `k`) and puts them back when the block is
disconnected.

**Creating stake: not needed.** The wallet's kernel search and `CheckProofOfStake()` read
the UTXO set and the block index only.

**What does need the past blocks** — refused with a message that names `fetchblocks` until
they are there:

| operation | without the past blocks |
|---|---|
| `rescanblockchain` from a height ≤ H, `importprivkey`/`importaddress`/`importpubkey` with rescan, `importwallet`, `importmulti` with a timestamp before the snapshot | error |
| a wallet file from elsewhere (restore, `-rescan`) | loaded; transactions after H are found; the wallet is marked as waiting and scanned from the first block when the past blocks have arrived |
| `getblock`, `getblockdeltas`, REST block, `gettxoutproof` for a block ≤ H | error |
| `getrawtransaction` of a transaction in a block ≤ H | "not found", with the hint |
| serving blocks ≤ H to peers | not done; the node does not advertise `NODE_NETWORK` |
| `-reindex`, `-reindex-chainstate` | refused at the start with a message (fetch the past blocks first, or use an empty data directory with `-snapshot=0`) |

Nothing in this list gives a *wrong* result silently: before this was guarded, a wallet
rescan over missing blocks simply found nothing, and a block request from a peer hit an
`assert`.

**Peers.** While the past blocks are missing the node clears `NODE_NETWORK` in its services.
3.0.x and 4.0 nodes do not start a header synchronization with such a peer and do not ask
it for blocks during their initial block download (`fClient`), and neither version requires
service bits for a connection, so the node keeps its connections, relays, and announces the
blocks it stakes. A request for a past block that arrives anyway is ignored.

### Fetching the past blocks

`fetchblocks` (RPC), `-fetchblocks=1` (at every start until they are there) or the button
under Settings downloads the block archives in the background and writes every block to
the place that the block index records for it, after checking that it is the block of the
chain (hash) and that its transactions fit the header (merkle root). The transaction index
is written for these blocks. **Nothing is validated again** and nothing is replayed. The
download can be interrupted; it goes on after the last complete archive. At the end,
wallets that wait are scanned, and from the next start the node advertises `NODE_NETWORK`.

`getsnapshotinfo` shows the state (`past_blocks`: `missing`, `downloading`, `present`).

The GUI asks before it downloads ("... needs the past blocks, about 1.4 GB. Download them
now?") when a restored wallet needs them, when keys are restored from recovery words,
when a multisig address is added with a search for earlier transactions, or when the
explorer is asked for a transaction that the node does not have. The state and the
button are in Settings, Network settings.

Restoring from recovery words (`importmnemonic`, the restore window) finds the funded
addresses in the UTXO set, which a snapshot node has, but the wallet needs the
transactions themselves to show and spend the coins, and those are in the past blocks:
the restore asks for the download first.

Undo data for the past blocks is not fetched (it cannot be verified without replaying).
A node that needs to reorganize below H, or to `-reindex`, starts over.

## 4. Options and RPCs

| | |
|---|---|
| `-snapshot=0` | never use a snapshot (default 1) |
| `-bootstrap=0` | no block archives and no snapshot |
| `-bootstrapurl`, `-bootstrapdir` | where the snapshot and the block archives are (12-bootstrap.md) |
| `-fetchblocks=1` | download the past blocks in the background after the start (default 0) |
| `-checkstaketime` | read the staked transaction of every block and compare its time with the coin (default: regtest only) |
| `-snapshotparams=height:block:utxo:tokens:governance:index` | regtest only: the accepted snapshot |
| `getstatehashes` | the hashes of the state of this node at its tip |
| `getsnapshotinfo` | accepted snapshot, snapshot height of this node, state of the past blocks |
| `fetchblocks` | start the download of the past blocks |

## 5. Making a snapshot

See the head of `contrib/aokchain/make_snapshot.py`. In short:

1. Synchronize a node **from the block archives only** to the height where they end:
   `connect=0`, `txindex=0`, `bootstrapurl=` or `bootstrapdir=`. The block index must hold
   nothing but the active chain, with the blocks in the files in the order of their
   heights; a node that got blocks from peers does not qualify, and a snapshot from it is
   rejected by the check.
2. `aokchain-cli getstatehashes > hashes.json`, then `aokchain-cli stop`.
3. `make_snapshot.py --datadir <dir> --hashes hashes.json --out <folder of the archives>`.
   It writes `snapshot-<height>.partNN` (190 MB at most each), `snapshot.json`, and the
   lines for `SHA256SUMS`, and prints the values for `chainparams.cpp`.
4. Put the values into `CMainParams` and build. Publish the files next to the block
   archives (same folder, same release).

The height must be one that the block archives reach, because the past blocks are fetched
from them.

Format: the parts are one gzip stream cut into pieces. The stream is a list of files —
length of the name (2 bytes, little endian), name, size (8 bytes), content — ended by a
name of length 0.

## 6. Faster validation of the blocks

A node that does not use a snapshot (`-snapshot=0`, a rejected snapshot, a `-reindex`)
validates the blocks. The import thread was sampled during a first synchronization from
the block archives (stack samples of the thread that connects blocks, heights 0.5 M to
1.2 M). The time went to:

| share | where | what was done |
|---|---|---|
| ~16–20 % | `GetTransaction()`: the staked transaction read from disk, twice per block | the time is taken from the coin |
| ~18 % | `ConnectTip()`: a copy of every token issued since the last flush, per block | the tokens of the block are walked instead |
| ~10 % | fee estimator, aging its statistics for every block | not during the initial block download |
| ~8–10 % | the date of the `UpdateTip` log line, formatted through a stream and a locale | formatted directly |
| ~12 % | block signature check | (needed) |

Measured on the same machine, same block archives over the LAN, to the tip of the main
network (3,032,675 blocks), peers connected so that `assumevalid` applies:

| | time to the tip |
|---|---|
| 4.0.0rc1 | 41 min 57 s |
| with these changes | 24 min 12 s |

The UTXO hash (`gettxoutsetinfo`) at the tip is equal to that of a node that was never
changed. The profile is from heights up to 1.2 M only (the sampler slowed the node down);
later heights were not profiled.

## 7. Measurements and checks

All on 2026-11-11/12, main network, files served over the LAN by plain http.

### The snapshot

| | |
|---|---|
| height, block | 3,031,432, `58a07d1f…909a` (the end of the published block archives) |
| unspent outputs | 7,274,962 |
| unpacked | 1,267.6 MB in 357 files (chainstate 580 MB, block index 607 MB, tokens 19 MB, governance 6 MB) |
| packed | 1,022.9 MB in 6 parts (5 × 190 MB + 73 MB) |
| block archives, for comparison | 1,409 MB in 15 files |

The databases are packed as LevelDB left them (not compacted).

### How the pinned values were obtained

* Node A: synchronized from the block archives with `connect=0`, `txindex=0`. Without
  peers there are no headers ahead of the tip, so `assumevalid` does not apply: every
  script of every block was verified. 2,884 s. `getstatehashes` at 3,031,432 gave the
  pinned values. This node's databases are the snapshot.
* Node B: the same kind of run with the default `txindex=1` and a build that read the
  staked transaction of every block from disk and compared its time with the coin
  (0 differences in 3,031,432 blocks). 4,176 s with benchmark logging. Same
  `hash_serialized_2` (`3188f23b…de4f`) and the same number of outputs as node A. (Its
  full hashes were not taken: `getstatehashes` got its final form after that run.)
* `feature_snapshot.py` shows on regtest that a node which validates the blocks reaches
  exactly the hashes of the snapshot, block index hash included.

### First start from the snapshot (daemon, new data directory)

| | build server | second server |
|---|---|---|
| download (LAN) | 8 s | 8.5 s |
| unpacking | 35 s | 35 s |
| loading the databases + check | ~45 s (the check itself 15 s) | ~45 s |
| RPC ready at height 3,031,432 | 90 s | 90 s |
| at the tip (1,260 blocks later, from peers) | **114 s** | **111 s** |
| data directory | 1.1 GB | 1.1 GB |
| `fetchblocks` (1.4 GB of archives, 3,031,433 blocks stored and indexed) | 259 s | 265 s |
| data directory afterwards | 3.5 GB | 3.5 GB |

For comparison, the same machine validating the blocks from the archives: 41 min 57 s
(4.0.0rc1), 24 min 12 s (with §6).

Checked after the start: `gettxoutsetinfo` (`hash_serialized_2`, `total_amount`) equal to
the mainnet verification node (which was synchronized block by block long ago) at heights
3,032,692 and 3,032,700; the node that validated all blocks with the changes of §6 equal
as well. After `fetchblocks`: `getblock` of blocks 1, 1,500,000 and 3,031,432 and
`getrawtransaction` of a transaction in block 1,500,000 byte-identical to the verification
node. After a restart the node advertises `NODE_NETWORK` and follows the tip.

The GUI wallet on this server, empty profile: language and data folder, then the splash
screen shows "downloading the snapshot, 380 MB / 1022 MB", unpacking, checking; then the
wallet passphrase step, the backup step, the main window.

### Not measured or not checked

* Download from the GitHub release (the snapshot is not uploaded yet): with the LAN the
  download is 8 s; over the internet it is what 1 GB takes.
* Windows: the Windows build compiles and was not run through a snapshot start.
* Receiving, sending and staking with a snapshot node on the main network (no coins
  there): done on regtest in `feature_snapshot.py`, including stakes of coins from before
  the snapshot validated by the snapshot node and stakes made by it.
* A 3.0.x node as a peer of a snapshot node: read from the 3.0.x source (§3), not run.
* An initial block download served by a snapshot node after `fetchblocks`: regtest only.
* The profile of §6 covers heights up to 1.2 M.

## 8. Consensus-relevant notes

1. **Trust anchor in the program.** `CMainParams` pins the snapshot (§1). A wrong value
   would make every new 4.0 node start from a wrong state. The values come from a node
   that validated every block and every script from the genesis block (§7); they should be
   reproduced independently before a release (`-snapshot=0`, synchronize, `getstatehashes`
   on a node stopped at that height, or compare `gettxoutsetinfo` at a common height as in
   §7).
2. **The time of a staked transaction comes from the coin** (`ConnectBlock()`,
   `CheckBlockStakeBeforeStorage()`), on every node, not only on snapshot nodes. The value
   is the same (`AddCoins()` stores `tx.nTime`); a coin given back by a disconnected block
   has time 0 and is handled as before (transaction read from disk). A build that read the
   transaction as well and compared found no difference in 3,031,432 main network blocks;
   `-checkstaketime` keeps comparing on regtest. This is a change in the block validation
   path, although not of a rule.
3. **A fork that begins at or below the snapshot height is refused** by a snapshot node
   (`bad-fork-prior-to-snapshot`), also when it is shallower than the maximum
   reorganization depth. This differs from other nodes only while the chain is less than
   500 blocks past the snapshot height, which is not the case for the pinned snapshot once
   the release is out.
4. **A snapshot node keeps time and coinstake mark of spent coins from before the
   snapshot** and restores them on a disconnect. An ordinary node restores such coins
   without them (existing behaviour, see below). On a snapshot node the coinstake maturity
   check and the `coin.nTime > tx.nTime` check in `tx_verify.cpp` therefore see the true
   values for these coins after a reorganization, where an ordinary node sees
   `fCoinStake = false` and time 0, which makes those two checks pass. A transaction that
   only the lenient form accepts (spending a coinstake output that is not mature, or
   carrying a time earlier than its input, right after a reorganization that restored that
   very coin) would be accepted by an ordinary node and rejected by a snapshot node. The
   same difference exists today between an ordinary node that went through the
   reorganization and one that did not (it synchronized later), so this adds no new kind
   of disagreement.
5. **Existing property, found during this work, not changed:** the undo data
   (`TxInUndoSerializer`) stores neither `nTime` nor `fCoinStake`. After a reorganization
   the restored coins have `nTime = 0` and `fCoinStake = false` in the UTXO database of
   the nodes that reorganized. Consequences: (a) the two `tx_verify.cpp` checks above are
   weaker on such nodes for such coins; (b) the full UTXO hash of `getstatehashes` (which
   covers both fields) can differ between honest nodes, while `hash_serialized_2` (which
   does not) is equal; (c) the wallet's kernel search uses `coin.nTime` and cannot find a
   stake for such a coin until it is spent and created again. Fixing the undo format is a
   consensus-relevant change of its own (06-roadmap.md §1) and was left alone.
6. **One pre-check is weaker on a snapshot node without the past blocks** (§3,
   `CheckBlockStakeBeforeStorage()` for fork blocks whose pre-snapshot kernel is already
   spent). `ConnectBlock()` is unchanged and decides.
7. **`NODE_NETWORK`.** Snapshot nodes without the past blocks cannot serve an initial
   block download (§9).

## 9. Limits

* The daemon cannot fetch from the built-in `https://` address (as for the block
  archives).
* A snapshot node that is asked to reorganize below the snapshot height refuses. This can
  only matter while the chain is less than 500 blocks past the snapshot height.
* While its past blocks are missing a node is of no use to a node that is doing its
  initial block download. A network needs enough nodes with all the blocks; nodes that are
  always on (exchanges, pools, stakers on servers) should run with `-fetchblocks=1` or
  without a snapshot.
* A new snapshot needs a new program version (the values are compiled in). Between
  releases a snapshot node has more blocks to fetch from its peers.
* `-verifysnapshot` (validating the whole chain in the background after a snapshot start
  and comparing) is not implemented. The equivalent check by hand: a second node with
  `-snapshot=0`, then `getstatehashes` on both at the same height.
