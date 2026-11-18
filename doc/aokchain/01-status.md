# 01. Current status and change history

- Branch: `4.0`. Its history continues from `master` of `AokChain/AokChain` at `5d1fd3d` ("Updated version", 2022-09-24)
- Client version: **4.0.10** (`_CLIENT_VERSION_IS_RELEASE=true`). The changes of every version are in `CHANGELOG-4.0.0.md`; the release notes of 4.0.0 are in `release-notes-4.0.0.md`

## Current release (2026-11-19): 4.0.10

- 4.0.0 to 4.0.10 are released. What each version changed is in the changelog; the sections from "At a glance" on are the record of 2026-11-11, when 4.0.0 was prepared.
- **4.0.10:** maintenance release for nodes that run for a long time. The last check of a found stake runs under the same locks as the search for it; a wallet leaves a coin out of its stakes when the chain state of the node does not hold it, and reports it (`coins_missing` in `getstakinginfo`); while many blocks are connected in a row the node waits for its internal notifications, so a rebuild of the chain state needs well under half the memory. After a stop without a normal shutdown, a forced stop of the machine included, a plain restart is enough: the start brings every store to the block of the chain state, the token database is written in one step, and the node checks at every start that its databases belong to one block and rebuilds from its block files by itself when it cannot go on (`-autorebuild`; a node that started from a snapshot starts over from it, a pruned node ends with a message). The governance commands of a block that leaves the chain are undone from a record of what they replaced. With `-addressindex` the address index follows the chain state after any stop. A setup program for Windows is new. No consensus rule, network message or wallet format changes. What the node does after an unclean stop is in `20-unclean-stop-recovery.md`.
- **Checks of 4.0.10:** 71 unit test suites and the functional baseline of 73 tests pass (eleven are new in it; `feature_auto_rebuild.py`, `feature_governance_reorg.py` and `feature_random_stops.py` are the largest). The test that stops a node at arbitrary moments ran 300 rounds with kills and 400 rounds with forced stops at the level of the block device; after every round the node had the state hashes, the freeze list, the tokens and the address index of its peer and of a node that never gave a block up, and in no round in which nothing was lost did the node rebuild. On a copy of the main network without a network connection, a rebuild of the chain state that was killed twice on the way went on after plain restarts and had the state hashes of the published snapshot at height 3,031,432; the same node then gave up 289,047 blocks and 1,870,832 more and had, each time, the state hashes of a node that was built up to that height only. A data directory was used by the 4.0.9 and the 4.0.10 programs in turn, with a wallet, tokens, frozen addresses and staking, and had the state hashes of its peer at every step.
- **4.0.9:** build release for macOS. The release builds on an Apple silicon Mac straight from the release tag, for macOS 11.0 and later (`doc/build-osx.md`). The node and the wallet behave as in 4.0.8; the Linux and Windows release builds find the same libraries as before.
- **Checks of 4.0.9:** 71 unit test suites and the functional baseline of 62 tests pass.
- **4.0.8:** maintenance release from the review before the public release. Token queries are coordinated with block processing; reorganizations and re-indexing with token transactions are exact; the address index stores a lock time for token transfers only (a node with `-addressindex` is started once with `-reindex`). The desktop wallet is available for Linux again, and the release programs are built from the release tag. The consensus rules and the chain state do not change. `-stakingwallet` names the wallets that stake when a node has several (since 4.0.7 a wallet that is opened while the node runs stakes as well).
- **Checks of 4.0.8:** 71 unit test suites and the functional baseline of 62 tests pass (`feature_tokens_undo.py`, `feature_activation_heights.py` and `rpc_tokens_concurrent.py` are new in it). The whole main network chain was read back with `-reindex` and gave the same tip and state hashes as before. A node started from the published snapshot (height 3,031,432) synchronized to the tip and followed it beside a 4.0.7 node, with equal state hashes at every comparison.
- **4.0.7:** the desktop wallet runs several wallets in one window (the wallet is chosen at the top of the side bar; Create, Open, Restore and Close Wallet; a staking switch per wallet; notifications and the console name the wallet). A wallet that is created or loaded while the node runs stakes when the node runs with `-staking`, and `unloadwallet` waits for the staking thread of the wallet. The consensus rules and the chain state do not change, and a node with one wallet behaves as before.
- **Checks of 4.0.7:** 71 unit test suites and the functional baseline of 59 tests pass (`feature_staking_multiwallet.py` and `wallet_multiwallet.py` are new in it). A first start on the published main network snapshot (height 3,031,432) in a new data directory, without a network connection, gives the hashes of `getstatehashes` that are compiled into the program, and the 4.0.6 programs read the same hashes from the same data. The GUI was checked with three wallets, in English and in Korean.

## At a glance (2026-11-11)

| Item | Original 3.0.5 | Now |
|---|---|---|
| Build (Ubuntu 24.04, gcc 13) | ✖ fails | ✅ C++17, 0 errors / 119 warnings |
| depends (release) build | Old versions (OpenSSL 1.0.1k etc.) | ✅ Boost 1.83, OpenSSL 3.0.15, libevent 2.1.12, ZeroMQ 4.3.5, miniupnpc 2.2.7, BDB 4.8 |
| `aokchain-tx`, `libaokchainconsensus.so` | ✖ build failure / undefined symbols | ✅ |
| Unit tests | ✖ do not compile | 73 / 78 pass (the failures are 5 in `bloom_tests`) |
| Functional tests | ✖ all fail at import | 38 pass (baseline), 0 regressions. PoS staking is tested on regtest as well (`feature_staking.py`) |
| Critical security issues | SEC-01, 02, 03, 05 | All 4 fixed (SEC-05: the core path) |
| Wallet performance (wallet with 30,000 transactions) | `getbalance` 67 ms, sending 152 ms, `listaddressgroupings` more than 11 minutes, staking thread CPU 26.5% | 2 ms / 18 ms / 1.4 s / 0.14% (document 08) |
| Staking thread of a wallet with no coins to stake | Uses 100% of a core, holds `cs_main` continuously | 0% |
| Mainnet verification | — | Full sync, history scan, full reindex with the final binary (`-par=8`) and following the live tip all pass |

## Mainnet verification results

| Verification | Method | Result |
|---|---|---|
| Signature validity of the historical chain | Full sync with `-par=1 -assumevalid=0` (3,030,485 blocks, about 4 hours) | Passed without stopping → the SEC-01 fix needs no fork |
| Historical violations of the candidate rules | `contrib/aokchain/history_scan.py` (height 3,031,015, about 11 minutes) | 0 violations (3,029,575 PoS blocks, 972,292 token outputs) |
| Tip following by the final binary | Live verification node (`1fbbd9c`, later replaced by `d5458c4`, `463c3bd`, `15a5694`; default parallel validation) | Follows the same tip as the peers (3.0.2, 3.0.5). Invalid PoS blocks are rejected before they are stored |
| Revalidation of the whole chain with the final binary | Offline node with `-reindex -par=8 -assumevalid=0` | All 3,031,060 blocks pass, 0 invalid (about 57 minutes). Block hash, 15,399 tokens and governance state (costs, fee address, freeze list) match the live node |

## assumevalid (updated 2026-11-11)

Mainnet `defaultAssumeValid` = block 3,030,000 `27a0660bf43381e7d367472a74d696873ff69bb36582031f9c2ed2ae1c58bdf1`, `nMinimumChainWork` = `…044f3a55ad8aa699012a`, `chainTxData` = (1794237264, 8,223,433 transactions, 0.0301 per second). Read with `getblockhash`, `getblockheader` and `getchaintxstats 43200` on the full revalidation node of the table above (tip 3,032,417) (commit `13222ef`). Update it the same way with every release. The checkpoints were not touched.

## Mainnet staking test (2026-11-11)
- **Node:** `<verify-dir>/` (4.0.0 candidate `15a5694`, with `staking=1` and `checkwalletlive=1` in `aokchain.conf`). The wallet backup is in `<verify-dir>/backup/`.
- **Funds:** 200 AOK of test coins. 100 AOK was split into 10 outputs (`4396386e…`) and 100 AOK was left as it was.
- **Status (block 3,031,938):** `getstakinginfo` → `staking: 1`, `search-interval: 16`, weight 190 AOK, network weight about 1.85 million AOK, expected time about 620,000 seconds (7.2 days). The staking thread used 0.01 seconds of CPU in 64 seconds.
- **How to check:** `grep "Successfully signed block" <verify-dir>/data/debug.log`, `aokchain-cli -datadir=<verify-dir>/data listtransactions "*" 20`. When a block is produced, check whether 3.0.x peers accepted it (the confirmation count in `getblock` increases) and how the wallet handles it if it becomes an orphan.
- **Stopping:** remove `staking=1` from `aokchain.conf` and restart.

## Commit history (since 3.0.5)

| Commit | Contents |
|---|---|
| `049b538` | build: switch to C++17, build fixes for gcc 13 / Boost 1.83 |
| `728ab58` | build: fix compiling and linking of `aokchain-tx` |
| `6db257c` | test: restore compilation of the unit tests, fix the consensus library layering, delete a duplicate directory |
| `a3687ef` | test: fix most unit test failures, fix the `reaokath` typo in the functional test framework |
| `b809e5a` | net: fix remote crash on a resent BLOCKTXN (SEC-03) |
| `b844eb7` | fix: uninitialized `addressType`, fix `getwalletinfo` |
| `0248be2`, `128cf0f` | qa: fix creation of the functional test cache, Python-side transaction and block serialization in the AokChain format |
| `2a7dad9` | test: bloom key test uses the AokChain secret key format |
| `938aec5` | net: support miniupnpc API 18 |
| `e5c3203`, `d4e3bb2` | validation: check the stake of a PoS block before storing it (fake stake). The second commit fixes a regression in the first implementation |
| `1c25384` | net: one GETHEADERS per INV, clamp the peer time offset |
| `0b23d4e` | **validation: restore block script verification (SEC-01)** + regression test |
| `cfd86ce` | contrib: history scan tool |
| `a016189` | **consensus: block token amount overflow (SEC-02/04/14)** + unit tests |
| `d5ccfef` | consensus: always check the value of token outputs, prepare the stake timestamp mask fork (not activated) |
| `a641851` | net: reject mutated blocks early, addr rate limit |
| `25c7554` | init: reset the token and governance databases on `-reindex-chainstate` + test |
| `1a11bda` | depends: update packages |
| `339a257` | build: 4.0.0 (release candidate), release notes |
| `1d870fb` | validation: fix `-reindex` dropping blocks that are out of order |
| `10a3fd7` | **validation: verify the signature of governance transactions before applying them (SEC-05)** + regression test |
| `1fbbd9c` | net, tokens: follow-up to the independent review |
| `1d028b4` | validation: fix null dereference crash in `ActivateBestChain` (SEC-23) |
| `d5458c4` | wallet: balance, coin selection and staking scans skip spent historical transactions + regression test (document 08) |
| `1afaf43` | test: regtest-only `-lastpowblock` (for testing the PoS phase) |
| `1d706e9` | miner: fix the busy loop, repeated searches and lock-free UTXO access of the staking thread (SEC-24) |
| `ed715ce` | wallet: exclude orphaned stakes, remove the full rebuild when a key is added, replace the `listaddressgroupings` algorithm, clean up `ZapSelectTx` (SEC-25), `-checkwalletlive` |
| `a103a30` | wallet: `purgeorphanstakes` RPC, `feature_staking.py` |
| `463c3bd` | qt: fix the locked balance cache bug (compilation not verified) |
| `04103d4` | miner: search for the kernel with an empty template, then build the full block (as in Qtum) |
| `643fa84` | wallet: `ZapSelectTx` in the form of bitcoin#13437/#18850/#34358, `DisableTransaction` and `CleanCoinStake` at startup in the Qtum form |
| `15a5694` | miner: fix `staking` and `expectedtime` of `getstakinginfo` always being 0 |
| `eb0651e` | qt: fix the balance signal connections of the send and token pages |
| `cc0c6ff` | rpc: extract private keys from a mnemonic, `mnemonictokeys`, `mnemonicfindaddress` (document 09) |
| Other `doc:` commits | These documents and the contributor working guide |

## Summary from the point of view of the consensus rules
- **The validity of the existing mainnet chain does not change.** This was confirmed by the verification in the table above.
- There are **rule tightenings that take effect immediately, without an activation height**: SEC-01 (signature verification), SEC-02 (token amount range), SEC-05 (governance signature). Normal use is not affected; the verdict differs from 3.0.x only when an attack block is created. For the cautions about the order of deployment, see the release notes and document 04.
- **Prepared but not activated:** the stake timestamp mask (S-05). On mainnet and testnet, `nStakeTimestampMaskHeight = INT_MAX`.

## Notes for operators (2026-11-11)
1. **Compare the governance state:** mainnet has about 99,600 frozen addresses, the token costs are root 1,000,000 / reissue 1,000,000 / sub 100,000 / unique 1 / username 1 AOK, and the fee address is `Kjd5rfNJ17gi3rFpRnZoaUq3XnULxMbSFg` (`freezelist`, `issuanceinfo`). In 3.0.x, governance changes were possible without the master key, so check that this state is what was intended.
2. **Source of the invalid PoS blocks:** PoS blocks whose kernel hash exceeds the target keep being broadcast on mainnet (about 50 in 4 hours). Find out whether this is a staker with a wrong clock or an attack attempt.
3. **Whether to turn the BIP37 (bloom) default off:** decide after checking whether mobile and SPV wallets depend on it.

## Open work

The phases of the work are in `06-roadmap.md`, the status of every security finding in `04-security-audit.md` and the upstream changes that are taken over in `07-upstream-backports.md`.
