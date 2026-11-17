# AokChain Core 4.0 changelog — compared with 3.0.5

- **Covers:** 4.0.0 to 4.0.8. The first part describes 4.0.0 as it was prepared; every later version has its own section at the end.
- **Baseline:** 3.0.5 (`5d1fd3d`, 2022-09-24)
- **Size of 4.0.0:** 136 commits (as of 2026-11-13)
- **Status:** released. The current version is 4.0.8 (2026-11-17). The Windows programs are not code-signed.

## Compatibility

- **No hard fork.** There is no activation height. The whole existing mainnet chain (about 3.03 million blocks) was revalidated from scratch with 4.0.0 and no block was rejected.
- **Connects to existing peers.** The network magic is unchanged; the protocol version goes from 70020 to **70021** (3.0.x accepts peers with a higher version). A mechanism that drops 3.0.x peers from a set height on is included but **disabled on mainnet and testnet** (`13-old-version-cutoff.md`). It was run against 3.0.2 and 3.0.5 peers and follows the tip.
- **Same wallet file format.** Release builds use BDB 4.8, so a 3.0.x `wallet.dat` opens as before.
- **Some rules are stricter.** Items 1 to 3 under "Security" reject blocks that 3.0.x wrongly accepts. Honest blocks are not affected, but an attack block would split 3.0.x from 4.0.0, so major stakers and exchanges should upgrade together.

## Security

1. **Block signature checks restored.** With the default multi-core setting, 3.0.x did not look at the result of signature verification for transactions in a block. A block containing a transaction with a bad signature was accepted.
2. **Token amount overflow closed.** Outputs whose sum wrapped around the integer range could create tokens out of nothing.
3. **Forged governance commands closed.** Because of item 1, address freezes, token cost changes and fee address changes could be applied without the master key. The signature is now verified before a command is applied.
4. **Remote crash fixed.** Receiving BLOCKTXN twice for the same block shut the node down.
5. **Crash while serving block requests fixed.** A peer's block request arriving right after another thread accepted a block shut the node down. This could happen occasionally on staking nodes.
6. **Fake-stake defence.** The kernel and stake input of a proof-of-stake block are checked before the block is written to disk.
7. **Mutated blocks rejected early.** A block with a wrong merkle root or block signature can no longer disturb the download state of other peers.
8. **P2P abuse limits.** Rate limit for addr messages, one GETHEADERS per INV, overflow-safe peer time offset.

## Staking

- **100% CPU fixed.** With an unlocked wallet that had nothing to stake (no balance, or everything immature right after a stake) the staking thread never slept, used a full core and slowed the whole node down. Reproduced on mainnet: 100% before, 0% after.
- **No repeated searches.** The kernel search is not repeated for the same block and the same 16 second slot. The full block is assembled only after a kernel was found (as in Qtum).
- **Locking fixed.** The kernel search read the UTXO cache without holding the locks.
- **`getstakinginfo` fixed.** `staking` and `expectedtime` were always 0, even while staking.
- **Orphaned stakes.** Stakes that lost against another block are left out of balance scans and are cleaned up when the wallet is opened (as in Qtum).

## Wallet performance

A wallet that had been used for a long time became very slow as transactions accumulated. Balance queries, sending and staking walked every past transaction of the wallet on every call. They now look only at transactions that still have spendable outputs. The results (balances, list of unspent outputs) are the same as before.

| Item (wallet with 30,000 transactions, regtest) | 3.0.x code | 4.0.0 |
|---|---|---|
| `getbalance` | 67 ms | 2 ms |
| `sendtoaddress` | 152 ms | 18 ms |
| `listaddressgroupings` | did not finish in 11 minutes | 1.4 s |
| Staking thread CPU | 26.5% of a core | 0.14% |

## New features

- **Private keys from a mnemonic.** Derives keys from the BIP39 recovery words of another wallet, such as the mobile wallet.
  - `mnemonicfindaddress`: finds the derivation path and private key from one address of that wallet.
  - `mnemonictokeys`: lists addresses and private keys below a path.
  - The checksum is validated against the English and Korean word lists. Nothing is stored.
- **Sign and multisig pages (GUI).** The "Sign" page of the wallet proves an address and checks a signature that was received, makes a multisig wallet or adds one you were invited to, and sends AOK from a multisig address with a signing request that is passed between the participants. It is built on the existing P2SH multisig and raw transaction RPCs; consensus and the wallet file format are unchanged. Tokens cannot be sent from multisig yet. See `11-multisig.md`.
- **Fast first synchronization.** (1) assumevalid moved to block 3,030,000. (2) Block bootstrap: block archives are fetched over HTTP(S) or read from a folder and imported, and every block is validated as usual. The GUI wallet has a built-in address for mainnet. `-bootstrapurl` (several mirrors), `-bootstrapdir`, `-bootstrap=0`, RPCs `getbootstrapinfo`/`skipbootstrap`, tool `contrib/aokchain/make_bootstrap.py`. See `12-bootstrap.md`.
- **Language choice.** A "Language" row at the top of the settings page (each language in its own language) with "Restart now"; the same selector in the first-run dialog (data directory).
- **`purgeorphanstakes`.** Deletes orphaned stakes that pile up in the wallet file (run manually).
- **`-checkwalletlive`.** Test option that verifies the wallet index against a full scan.
- **`-lastpowblock`.** Lets regtest reach the proof-of-stake phase (regtest only).

- **Chain state snapshot (fast start).** A new installation starts from the chain state at block 3,031,432 instead of validating every block. The program accepts a snapshot only when the hashes of its unspent outputs, tokens, governance state and block index equal the values compiled into it; otherwise it removes the snapshot and validates the blocks. The blocks before the snapshot are downloaded only when something needs them (`fetchblocks`, `-fetchblocks=1`, or the question the wallet asks). `-snapshot=0` turns it off. RPCs `getstatehashes`, `getsnapshotinfo`, `fetchblocks`; tool `contrib/aokchain/make_snapshot.py`. See `15-snapshot.md`.
- **Faster block validation** for nodes that validate the chain: the staked transaction is no longer read from disk for every block, and three per-block costs that were not validation are gone. See `15-snapshot.md` §6.

## Bug fixes

- **`-reindex`:** stopped at a block stored before its parent (at height 3,440 with mainnet data).
- **`-reindex-chainstate`:** did not wipe the token and governance databases and marked valid blocks invalid.
- **`removeprunedfunds`:** left traces of the removed transaction that could later shut the node down.
- **GUI:** the balance on the Send and token pages was never refreshed; locked balance display.
- **`getwalletinfo`:** printed `hdmasterkeyid` when there was no HD seed.
- **Address index:** use of an uninitialised value.
- **`aokchain-tx`, `libaokchainconsensus`:** build and link errors.

## Build and dependencies

- **Compiler:** C++17. Builds on current systems (Ubuntu 24.04, GCC 13). 3.0.5 did not build there.
- **Dependencies:** Boost 1.64 → 1.83, OpenSSL 1.0.1k → 3.0.15, libevent 2.1.8 → 2.1.12, ZeroMQ 4.2.2 → 4.3.5, miniupnpc 2.0 → 2.2.7. BDB stays at 4.8.30.
- **Windows build:** Qt 5.7.1 → 5.15.10.

## Tests (4.0.0, as of 2026-11-11)

- **Unit tests:** 73 of 78 pass. 3.0.5 did not compile them. The remaining 5 need new bloom filter test data.
- **Functional tests:** a baseline of 38 passes (including the multisig flow `wallet_multisig_flow.py`, the bootstrap `feature_bootstrap.py` and the old peer cutoff `p2p_old_peer_cutoff.py`). None ran on 3.0.5. The 47 that still fail expect Bitcoin or Ravencoin values and have not been ported.
- **Mainnet:** full chain revalidation, history scan, live tip following, two real payments confirmed.

## Known limitations of 4.0.0 (as of 2026-11-11)

Several of these changed before the release and in later versions: see the sections below.

- **Peer discovery:** the DNS seed does not answer and most fixed seeds are dead, so a fresh install may need peers added with `addnode`.
- **Mainnet staking:** staking is running on mainnet but has not produced a block yet, so acceptance of a block made by 4.0.0 is not confirmed.
- **Windows:** tested under wine only. Long-term operation on real Windows is not confirmed.
- **Mnemonics:** only English and Korean are validated. The derivation scheme of the AOK mobile wallet is not published.
- **Not code-signed.** Windows SmartScreen shows a warning.

## Update of 2026-11-13: added in rc3 to rc7

### Staking
- **Coins that lost their time in a reorganization.** 3.0.x lost the transaction time of a coin when a block was disconnected, and such a coin then produced only invalid stake blocks (invalid blocks at 21.5% of the main network heights; 4,659 and 3,748 damaged coins on the two large staking nodes). 4.0 keeps the time, stakes damaged coins correctly and repairs the chain state with `repaircoins`. With both nodes on 4.0 there are no invalid blocks (`16-stake-coin-time.md`).
- **Size of the staking coins**: `-stakecombinethreshold` (default 1,000 AOK, was fixed at 100). A staking wallet has about a tenth of the coins.
- **`getstakinginfo` and the side bar show the last 24 hours**: stakes, lost stakes, reward.
- **Offline staking validation (SEC-26).** The holder of a staking key could take or destroy delegated coins; closed. No activation height, because the main network never used the feature (`18-offline-staking.md`).

### Wallet and tools
- `aokchain-wallet`: look into a wallet file, create one or salvage one, without a node.
- `unloadwallet`, `restorewallet`. `backupwallet` copied the file of another wallet for a wallet in a folder of its own; fixed.
- `importprivkey` on a snapshot node imports the key and waits for the past blocks.
- The staking-only unlock is a state of each wallet (SEC-13).

### Commands and options from Bitcoin Core 0.17 to 30 (`17-upstream-features.md`)
- `scantxoutset`, `testmempoolaccept`, `getblockstats`, `getindexinfo`, `getnodeaddresses`, `getrpcinfo`, `getaddressinfo`, `getbalances`, `signrawtransactionwithwallet`, `signrawtransactionwithkey`
- a fee rate limit for `sendrawtransaction`
- `-rpcwhitelist`, `-daemonwait`, `-startupnotify`, `-shutdownnotify`, `-logratelimit`

### Security (this update)
- **SEC-08:** the difficulty field of a proof-of-stake block is checked for every header (it was skipped during the initial synchronization). Blocks 1,443 to 3,031,432 all have the value of the formula.
- **SEC-15:** the upper bound of an amount is 25 billion AOK instead of the largest 64 bit number, so the range checks mean something.
- **SEC-17:** checkpoints up to 3,030,000; the fixed seeds are rebuilt from nodes that answer.
- **A smaller allowance for the time of a block** (10 minutes to 60 seconds) as a rule with an activation height; not scheduled on the main network.

### GUI
- Plainer design (grey ramp, weights 500/600); the past block download in the side bar.
- The staking switch showed "off" after a start with `-staking=1` while the wallet staked; fixed.

### Tests
- The unit test suites run one by one. "73 of 78 pass" was the count of a run that stopped half way; in fact **57 of 70 suites pass** (four were repaired now), and the 13 that fail use Bitcoin or Ravencoin test data (`05-stability-testing.md`).
- The functional baseline has 47 tests (48 with rc8).

### The reward schedule
- The next halving is at height 3,155,760 (mid February 2027); the subsidy ends at height 5,259,600 (mid 2031).
- **Postponing the halving is a hard fork.** The block reward would be larger than the old rule allows, and nodes of older versions reject such a block. It loosens a rule instead of tightening one, so it needs an activation height and the upgrade of every node and exchange. It is not in this update.

## Update of 2026-11-13 (2): rc8

The releases and security advisories of Bitcoin Core 30.0 to 32.1 and of Ravencoin 4.7 and 4.8 were reviewed, and what applies was taken over.

- **Orphan transactions (CVE-2024-52914).** When a parent arrived, every orphan that waited for it was validated on the spot. They are now processed one at a time, between the messages of that peer, so a peer cannot prepare orphans that keep the node busy. The outcome (what ends up in the mempool) is the same (`p2p_orphan_processing.py`).
- **Urgent TCP data (CVE-2026-117350).** The exceptional condition of a socket was watched, and read on, also for a peer whose receiving was paused, which let data in past the receive limit. A paused peer is neither watched nor read.
- **Announcement backlog.** The more transaction announcements wait for a peer, the more are sent at once (the whole set is sorted anew in every round).
- **Wallet names.** `createwallet` and `loadwallet` refuse `.` and `..` in a name.
- **`-dbcache` default.** 1024 MiB instead of 450 on a computer with 4 GiB of memory or more.
- **Cache split.** The governance database took half of the remaining cache; it gets 8 MiB now, which about doubles the cache of unspent outputs for the same `-dbcache`.
- **Unit tests.** `key_tests` and `pow_tests` were adapted to AokChain: 59 of 70 suites pass.

Reviewed and not taken over:
- **Ravencoin 4.7/4.8, asset transfer quantity overflow** (the holder of one unit of a token could make 2^64 of them; published in August 2026): 4.0 has the same checks already (SEC-02). **3.0.x nodes are vulnerable.**
- **Ravencoin 4.8, mining bypass**: proof-of-work (KAWPOW) only, does not apply.
- **Announcement queue of paused pre-BIP31 peers (CVE-2026-117349)**: does not apply, the lowest accepted protocol version is past BIP31.
- **Undo file position overflow (CVE-2026-117346)**: applies and is not fixed. The undo data of one block file has to exceed 4 GiB, which needs a staker who builds a series of blocks that spend very many outputs with large scripts. The fix (64 bit positions) is outstanding.

## Update of 2026-11-13 (3): rc9

- **Limit on stored blocks per stake (SEC-28).** A node stored any number of different blocks signed with one stake; it now stores four (`-maxblocksperstake`). A further block is fetched and stored when a block builds on it. The consensus rules are unchanged.
- **Misbehaving peers (CVE-2020-14198).** A peer that broke the rules was put on the ban list for 24 hours; it is now recorded in a filter of fixed size in memory. The ban list is written to disk and has no limit, so an attacker with many addresses could grow it without end. A recorded address is refused only while the inbound slots are almost full, and the record is gone after a restart. `setban` and `listbanned` deal with the bans the operator made.
- **Uninitialized script type.** `Solver()` set its second type for token scripts only, so the standard input check and wallet signing compared an uninitialized value (found with the sanitizers). Not a consensus path.
- **Fee rate overflow.** `CFeeRate` overflowed 64 bits for amounts above 92 million AOK.
- **Fee estimates.** The decision to skip estimates during the first synchronization moved from the mempool to the block connection code (same behaviour).
- **Governance test.** With the regtest-only `-masteraddress` a test holds the master key; freeze, unfreeze, issuing cost, fee address and block disconnection are tested (`feature_governance.py`). A test of issuing, transferring and reissuing tokens was added (`feature_tokens.py`).
- **Unit tests.** All 70 suites that have test cases pass (`05-stability-testing.md`). Two cases on Bitcoin signature vectors are not run.
- **Sanitizers.** Every unit suite was run under the address and undefined behaviour sanitizers.
- The functional test baseline has 51 tests.

Note: some boolean fields of RPC results are printed as `1`/`0`, not `true`/`false`, since 3.0.x (`networkactive`, `complete`, `staking` and others). This is kept for compatibility with programs that read them.

## 4.0.1 (2026-11-14)

Fixes after the 4.0.0 release, including the results of the external source review of 2026-11-13.

- **`walletpassphrase` (SEC-29).** A call that failed because of its arguments left the wallet open for payments; the arguments are checked first now. The mark that takes a wallet back to staking only after a timed unlock is per wallet.
- **Atomic governance state (SEC-30).** Governance commands were written to the database while a block was being checked, so a block that was then refused, a block that was only checked, and the check of the last blocks at startup could change the freeze list and the issuing cost of a node (which then refused valid blocks). The commands of a block are now collected and written at once when the block is connected. The result of a valid block is the same as before.
- **Freezes and block production (SEC-31).** A freeze command and a payment from the address it freezes, both in the mempool, made block assembly produce an invalid block, and a staker could make no valid block while both were there (the owner of the address could cause it with one payment with a smaller fee). Block assembly no longer puts both into one block, the payments of a frozen address are removed from the mempool after the freeze, and a staker checks its block before it sends it.
- **Blocks per stake, revised.** Deciding whether a block beyond the limit is stored no longer walks the whole block index (a stream of blocks with one stake could keep a node busy). A block beyond the limit is stored when it is on the best chain of headers.
- **`getblocktemplate`.** It passed the segwit flag where block assembly expects the proof-of-stake flag, so every template was assembled unchecked with the proof-of-stake difficulty. Templates are now proof-of-work templates and are checked; when the next block is a proof-of-stake block the call returns an error.
- **Pinned snapshot files (SEC-32).** The SHA-256 of the published snapshot files is in the program; other files are neither unpacked nor opened.
- **Relock timer (SEC-33).** The relock timer of `walletpassphrase` is handled after the locks are released, which removes a deadlock that could stop the node when the timer ran at that moment. The timer table has a lock, and an outdated timer does not lock a wallet that was opened again.
- **Diagnostics.** The duration of the first stake search after a start is logged. `-debug=coinstake` works.
- **`upgrade.sh`.** Waits for the process ids of a node that does not run under systemd. A new installation can start from the snapshot with `AOK_SNAPSHOT`. The newest three backups are kept.
- **Recovery words of the AOK mobile wallet.** That wallet makes its 12 words without the BIP39 checksum (the seed is made from the words as BIP39 says), so the restore page refused them: "the words do not fit together". When every word is in the word list and only the checksum is wrong, the page now says so and offers to search without it; `importmnemonic` takes `validate=false` for the same. Unknown words and a wrong number of words are still refused.

## 4.0.2 (2026-11-14)

- **Restoring from recovery words is hidden.** A payment from keys restored from the recovery words of another wallet (the AOK mobile wallet) keeps its change in this wallet's own keys, which that wallet does not see, so its coins look lost there. Until it is decided how recovery words and the keys of this wallet fit together, the settings page does not offer the restore. `-mnemonicrestore` shows it again; the RPCs (`importmnemonic`, `mnemonicfindaddress`, `mnemonictokeys`) stay.
- **Passphrases in English letters, digits and symbols.** The passphrase fields (setting, unlocking, changing) have no input method, accept printable ASCII only (the space included) and say so when another character is typed ("Check the Korean/English key"). A wallet whose passphrase contains other characters is unlocked with `walletpassphrase` in the console.
- Nothing changes for nodes: the daemon is the same as in 4.0.1.

## 4.0.3 (2026-11-14)

- **Back and forward.** Every page header has back and forward buttons, as a web browser has. They go through the pages that were shown, the look-ups of the explorer (a block, a transaction of it, an address, and back, to where a result was scrolled) and the sub pages of Sign. Alt+Left and Alt+Right (Cmd+[ and Cmd+] on a Mac) and the back and forward buttons of a mouse do the same. The tool tip of a button names where it leads; the side bar follows.
- **A status light on every page**, next to the title. Green "Up to date": the node has peers and a block of the last 90 minutes. Amber: it synchronizes, reindexes or rescans the wallet, or the page is reading its contents. Red: no connection, the network turned off, or the page could not read its contents. The tool tip gives the block, its age, the peers and when the page was read.
- **Refresh.** The pages that read data (overview, transactions, tokens, create and manage tokens, explorer, sign) read it again on a click on their light or with F5 (Cmd+R on a Mac); the light shows "Refreshing" meanwhile and the time of the read after it.
- The sub pages of Sign show the way up in the title ("Sign > Prove my address"). Opening a multisig wallet or a signing request that is gone showed an empty sub page; it now shows the first page of Sign.
- Nothing changes for nodes: the daemon and `aokchain-cli` are the same as in 4.0.1 and 4.0.2.

## 4.0.4 (2026-11-14)

RPC only; the rules and the data do not change. For the nodes whose address index RPCs are used (the explorer and API node); stakers can stay on 4.0.1.

- **The address index calls take `{"addresses": [...]}`**, as their help says: `getaddressbalance`, `getaddresstxids`, `getaddressutxos`, `getaddressmempool` and `getaddressdeltas` answered any object with "Invalid address" (since 3.0.x), so `getaddressdeltas` could not be used at all. One address (or username) as a string works as before; `aokchain-cli` reads the argument as JSON only when it starts with `{` or `[`. An address given twice counts once. `getaddresstxids` takes `start` and `end` heights in the object.
- **`getaddressutxos` in pages:** `{"limit": n, "after": "<next>"}` returns at most n outputs and `next` for the following page (outputs in the order of block height, transaction id and output index). The object also takes `chainInfo` (hash and height of the tip), `amount` and `tokenName`; the amount and token name after an address stay.
- **`mediantime`** of `getblock` and `getblockheader` is the median time past; it was the block time. Time locks are measured against the median time past.
- `rpc_addressindex.py` passes and is in the test baseline (54 tests).

## 4.0.5 (2026-11-14)

The address index only; the rules and the chain state do not change. For the nodes that keep an address index (`addressindex=1`).

- **Stake coins missing from the address index.** When a block was disconnected, the pay-to-pubkey coins it had spent were not given back to the address index: the branch for such inputs was a copy of the one for outputs. Every stake pays to a public key, so each orphaned stake left its input missing from `getaddressutxos` and too low in `getaddressbalance`, although the coin is unspent (`gettxout`). On the explorer node 2,463 outputs (358,205.75 AOK) were missing. The address index of a node that has already disconnected such blocks has to be built again (`-reindex`).
- `rpc_addressindex.py` spends a pay-to-pubkey output, disconnects the block with `invalidateblock` and checks the unspent index and the balance; it fails without the fix.

## 4.0.6 (2026-11-16)

Small fixes of the node, the RPC commands and the GUI; the rules and the chain state do not change. RPC callers should read the first three items.

- **`lock_time` of `sendtoaddress` and `sendfrom`.** A lock (`<lock_time> OP_CHECKLOCKTIMEVERIFY OP_DROP`) is put in front of the output script only from 256 on, so a value from 1 to 255, or a negative one, made an output without a lock and without a word. These values are refused now: error -8, "Invalid lock_time N: use 0 for no lock, or a block height or a time of at least 256". 0 is still no lock; the outputs of 0 and of 256 and more are the same as before.
- **`conf_target` and `estimate_mode` of `sendtoaddress` and `sendmany`** were read one place behind the argument table and always failed ("JSON value is not an integer as expected", "... not a string as expected"); they work now. The help of `sendtoaddress` numbers its arguments right, and the help of both says that the message is written into the transaction (version 2, at most 120 bytes) and can be read by anyone.
- **Tokens to an offline staking address.** Its script takes no token data, and consensus refuses a token output there (`bad-txns-bad-token-script`). A transfer failed with -25 "Transaction amount too small", an issue, a reissue or a unique token only when the transaction was sent (-4, `bad-txns-bad-token-script`). Every token command (`transfer`, `transfermany`, `transfermanyoutputs`, `transferfromaddress`, `issue`, `issueunique`, `registerusername`, `reissue`) and `createrawtransaction` refuse such an address before anything is built: -5, "Tokens cannot be sent to an offline staking address: <address>"; the change address that gets the owner token back is checked too. Coins still go to offline staking addresses.
- **`registerusername`** answers a name that is not a username with "Invalid username: <name>" and the rule: @ followed by at least 4 characters of A-Z 0-9 _ . (at most 31 with the @). It named rules that are not checked. The rule itself is unchanged.
- **`issueunique` below a sub token:** `issueunique "PARENT/SUB" ["tag"]` issues `PARENT/SUB#tag` (it failed with "token_name must only consist of valid characters and have a size between 3 and 30 characters"). Consensus accepted such tokens before (`issue "PARENT/SUB#tag"`). The help says that the fee goes to the token fee address.
- **`aokchain-cli`** without a running node says "couldn't connect to server" (it said "Could not locate RPC credentials"), and `-rpcwait` waits while a starting node has not written its cookie yet.
- **An index turned on or off together with `-reindex-chainstate`.** The flags of the indexes are kept in the block index, which only `-reindex` builds again. The start stops with "You need to rebuild the database using -reindex to change -<index>" (for `-addressindex`, `-spentindex` and `-timestampindex` it named `-reindex-chainstate`, which gives the same error again), and it no longer wipes the token database before it stops: the next start found the blocks inconsistent, and only `-reindex` helped. Checked on a copy of a main network node: the refused start leaves the state hashes as they were, and a real `-reindex-chainstate` gives the same utxo, token and governance hashes again.
- **`removeaddress`** does not rescan on a pruned node (it rescanned from the first block); its help is that of `removeaddress` (it was that of `importaddress`).
- The help of `createrawtransaction` has AOK examples (the token fee address and its fees) instead of the Ravencoin burn addresses, and valid JSON.
- **GUI:** typos on the token pages ("Check Availabilty", "Select an token", "Can Reisssue", "Quantity is to large" and others); the quantity limits of Create Tokens and Reissue follow consensus (25,000,000,000; they were 21,000,000,000); large quantities are exact (24,999,999,999 became 24,999,999,999.00000256); the multisig backup help names the Wallet security card; the token send page says that tokens cannot go to an offline staking address.
- New tests `wallet_send_arguments.py`, `interface_cli_rpcwait.py` and `wallet_removeaddress.py`; `feature_reindex.py`, `feature_reindex_tokens.py`, `feature_tokens.py` and `wallet_importprunedfunds.py` check the other changes. 57 functional tests and all 71 unit test suites pass.

## 4.0.7 (2026-11-17)

Several wallets in one window of the desktop wallet. The rules and the chain state do not change, and a node with one wallet behaves as before.

- **GUI: several wallets in one window.** The window showed the first wallet of the node only. The wallet that is shown is now chosen at the top of the side bar, which names it and its lock state. Its menu lists the open wallets and has Create Wallet, Open Wallet (the wallets of the wallet folder that are not open, and Restore Wallet for a backup file) and Close Wallet; the Settings page has the same in the card "Your wallets", with the file of the wallet. Every page follows the wallet shown: overview, send, receive, transactions, tokens, create and manage tokens, explorer, address book, sign and multisig, settings. With several wallets open, the window title, the notifications of incoming and sent transactions and the console (it has a wallet selector) name the wallet.
- **Create, open, restore, close.** A new wallet can be encrypted right away. Encrypting a wallet that is not the first one needs no restart: it is closed and opened again, then the backup is offered. The first wallet of the node restarts the program as before and cannot be closed. The wallets that were open are opened again at the next start, and the one shown last is shown again. With `-wallet` in the configuration or on the command line exactly those wallets are loaded and nothing is remembered. A wallet that a command loads or unloads while the window is open (`loadwallet`, `createwallet`, `restorewallet` or `unloadwallet`, in the console or from another program) appears in the window or leaves it within a few seconds.
- **A wallet of its own for owner tokens.** Owner tokens (`NAME!`) can be kept in a separate wallet: tokens are created and reissued from it, and the wallet that is used every day cannot do either by mistake. The token pages work with the wallet that is shown; each wallet has its own tokens, addresses, multisig wallets and passphrase.
- **Staking.** Every loaded wallet stakes on a thread of its own, as the wallets given at the start did. The switch in the side bar is that of the wallet shown, and the side bar says how many other wallets stake. New: a wallet that is created or loaded while the node runs (`createwallet`, `loadwallet`, `restorewallet`, or the GUI) stakes when the node runs with `-staking`; before, only the wallets of the start staked. `unloadwallet` waits for a kernel search that is running (at most a minute) before the wallet file is closed, and "The first kernel search took ..." is logged once per staking thread.
- **`loadwallet`, `createwallet`, `restorewallet`, `unloadwallet`** share their code with the GUI. Their answers and error codes are unchanged. Unloading a wallet whose database environment nobody else uses removes the log files of that environment, as the shutdown does: the wallet file holds everything.
- **GUI texts.** English plural forms for texts of 4.0 that showed "(s)" ("1 kind" and "2 kinds" of tokens, "24 h: 1 reward"). A wallet that is unlocked for staking only says "Staking only" under its name. The multisig page says "Only AOK can be sent from a multisig address. Tokens cannot be sent from one."
- New test `feature_staking_multiwallet.py` (two wallets on regtest proof of stake); `wallet_multiwallet.py` checks the wallet folders, the endpoints and the errors of the wallet commands. Both are in the functional baseline (59 tests).

## 4.0.8 (2026-11-17)

Maintenance release from the review before the public release. The rules do not change: 4.0.8 accepts the same blocks as 4.0.0 to 4.0.7, and the chain state is not touched. Upgrade every node.

- **Token queries.** The commands that read token data are coordinated with block processing, and listing tokens is a little faster.
- **Reorganizations and re-indexing.** Disconnecting blocks and reading blocks back from disk (`-reindex`, `-reindex-chainstate`) are exact for every kind of token transaction: issues, reissues, transfers, sub tokens, unique tokens, usernames and owner tokens.
- **Address index.** Token outputs other than transfers are stored without a lock time. A node that runs with `-addressindex` should be started once with `-reindex` after the upgrade, which corrects the records of earlier blocks; new blocks are right without it, and `getaddressutxos` was not affected. Other nodes need nothing.
- **Staking wallets.** `-stakingwallet=<name>` names the wallets that `-staking` stakes with; it may be given several times, and without it every wallet stakes, as before. Since 4.0.7 a wallet that is opened while the node runs stakes like the wallets of the start. A node that stakes with one wallet and opens others for a service names its staking wallet: the others do not stake, whether they are opened at the start or later (`19-upgrade-guide.md`).
- **Desktop wallet.** The staking switch shows "on" only once staking has started, not while the passphrase is being asked for. The steps of the multisig wizard have a row of their own, so its title fits a window of 1280 px. The units are named AOK, Milli-AOK and Micro-AOK; "username" is one word everywhere; the passphrase hint names no keyboard key.
- **Desktop wallet for Linux.** The Linux release has the desktop wallet again, as 3.0.5 had: `aokchain-qt` in an archive of its own, with Qt linked in. It uses the X and font libraries that an Ubuntu 22.04 or 24.04 desktop installs, and types other scripts through IBus. To build it, depends has the Linux libraries of Qt 5.15 and configure finds the parts of a static Qt.
- **Help texts.** Every example uses AokChain's ports and example addresses that belong to nobody; amounts are in AOK. A token name that is too short or reserved is refused with that reason (the error codes are unchanged). `-staking` and `-advancedui` are in the help, and the man pages are those of the current programs.
- **Version text.** A build from a source archive names its own commit. The release programs are built from the release tag and report the plain version, `v4.0.8`.
- **Documents.** The links to upstream documents work again, and `doc/README.md` describes the 4.0 packages and the documents that exist.
- New tests `feature_tokens_undo.py`, `feature_activation_heights.py` and `rpc_tokens_concurrent.py`; `feature_staking_multiwallet.py` checks `-stakingwallet`. The functional baseline has 62 tests.
