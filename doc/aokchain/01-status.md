# 01. Current status and change history

- Working branch: `modernize` (original `master` = `5d1fd3d` "Updated version", 2022-09-24)
- Repository: private GitHub repository `AokChain/AokChain4`, branch `4.0` (history continues from the public `AokChain/AokChain` at `5d1fd3d`; nothing is pushed to the public repository yet)
- Client version: **4.0.0 release candidate** (`_CLIENT_VERSION_IS_RELEASE=false`). The release notes are in `release-notes-4.0.0.md`

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

## Mainnet staking test (started 2026-11-11, in progress)
- **Node:** `<verify-dir>/` (4.0.0 candidate `15a5694`, with `staking=1` and `checkwalletlive=1` in `aokchain.conf`). The wallet backup is in `<verify-dir>/backup/`.
- **Funds:** 200 AOK of test coins. 100 AOK was split into 10 outputs (`4396386e…`) and 100 AOK was left as it was.
- **Status (block 3,031,938):** `getstakinginfo` → `staking: 1`, `search-interval: 16`, weight 190 AOK, network weight about 1.85 million AOK, expected time about 620,000 seconds (7.2 days). The staking thread used 0.01 seconds of CPU in 64 seconds.
- **How to check:** `grep "Successfully signed block" <verify-dir>/data/debug.log`, `aokchain-cli -datadir=<verify-dir>/data listtransactions "*" 20`. When a block is produced, check whether 3.0.x peers accepted it (the confirmation count in `getblock` increases) and how the wallet handles it if it becomes an orphan.
- **Stopping:** remove `staking=1` from `aokchain.conf` and restart.

## Commit history (`modernize`, since the original)

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

## To be checked by the operators
1. **Compare the governance state:** mainnet has about 99,600 frozen addresses, the token costs are root 1,000,000 / reissue 1,000,000 / sub 100,000 / unique 1 / username 1 AOK, and the fee address is `Kjd5rfNJ17gi3rFpRnZoaUq3XnULxMbSFg` (`freezelist`, `issuanceinfo`). In 3.0.x, governance changes were possible without the master key, so check that this state is what was intended.
2. **Source of the invalid PoS blocks:** PoS blocks whose kernel hash exceeds the target keep being broadcast on mainnet (about 50 in 4 hours). Find out whether this is a staker with a wrong clock or an attack attempt.
3. **Seed nodes:** the DNS seed `dns.aok.network` does not resolve, and only 3 of the 11 fixed seeds respond.
4. **Deployment plan:** upgrade schedule of the major stakers and exchanges, testnet observation period, activation height of S-05.
5. **Remote repository:** the work is kept in the private repository `AokChain/AokChain4`. Publishing to the public repository waits for the coordinated rollout (see `13-old-version-cutoff.md`).
6. **Whether to turn the BIP37 (bloom) default off:** decide after checking whether mobile and SPV wallets depend on it.

## Next steps (in order of priority)
1. Deploy the 4.0.0 candidate on testnet (or a private test network) and observe it, including staking
2. SEC-06 phase 2 (block budget for deep forks, header spam), the rest of SEC-05, a random number generator without OpenSSL
3. Tor v3 + addrv2, removal of UPnP, discourage-style banning
4. Check the result of the mainnet staking test (section above): acceptance of the first stake block, handling of orphaned stakes
5. Sort out the 47 failing functional tests (economic parameters, Raven names), regenerate the `bloom_tests` vectors
6. Qt depends for Linux (the xcb packages), test operating the GUI on screen (the Windows cross build with static Qt 5.15.10 is done and merged; the Linux GUI build was confirmed with system Qt 5.15)
