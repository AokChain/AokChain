# AokChain Core 4.0.0 (candidate) changelog — compared with 3.0.5

- **Covers:** 4.0.0 release candidate (`modernize` branch, Windows test build `v4.0.0.0-0698aa2`)
- **Baseline:** 3.0.5 (`5d1fd3d`, 2022-09-24)
- **Size:** 136 commits (as of 2026-11-13)
- **Status:** pre-release. Not yet observed on testnet and not code-signed.

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
- **Windows build:** Qt 5.7.1 → 5.15.10. This part lives in a separate work tree and is not merged into `modernize` yet.

## Tests

- **Unit tests:** 73 of 78 pass. 3.0.5 did not compile them. The remaining 5 need new bloom filter test data.
- **Functional tests:** a baseline of 38 passes (including the multisig flow `wallet_multisig_flow.py`, the bootstrap `feature_bootstrap.py` and the old peer cutoff `p2p_old_peer_cutoff.py`). None ran on 3.0.5. The 47 that still fail expect Bitcoin or Ravencoin values and have not been ported.
- **Mainnet:** full chain revalidation, history scan, live tip following, two real payments confirmed.

## Known limitations

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

## 4.0.1 (in preparation)

Fixes after the 4.0.0 release, including the results of the external source review of 2026-11-13.

- **`walletpassphrase` (SEC-29).** A call that failed because of its arguments left the wallet open for payments; the arguments are checked first now. The mark that takes a wallet back to staking only after a timed unlock is per wallet.
- **Diagnostics.** The duration of the first stake search after a start is logged. `-debug=coinstake` works.
- **`upgrade.sh`.** Waits for the process ids of a node that does not run under systemd. A new installation can start from the snapshot with `AOK_SNAPSHOT`. The newest three backups are kept.
