# 05. Stability and state of the tests

## 1. Unit tests (`src/test/test_aokchain`)

In the original repository the unit tests **did not even compile** (they were off by default in configure).
**Correction of 2026-11-13.** "73 of 78 pass", the figure used as the baseline so far, was not the whole picture. `test_aokchain` in one run stops at the fifth failing case of `bloom_tests`, which is a memory access violation, and the suites behind it (about 218 of 296 test cases) **were never run**. "78" was the number of cases up to that point. `wallet_tests`, `rpc_tests`, `script_tests` and others had therefore never served as a regression check.

The suites are run one by one now:
```bash
contrib/aokchain/unit-tests.sh        # each suite on its own, compared with doc/aokchain/unit-baseline.txt
```
Current state: **57 of 70 suites pass, 13 fail.** The 57 are the baseline (`unit-baseline.txt`); if one of them fails, that is a regression. (53 passed when this was first looked at; `crypto_tests`, `hash_tests`, `main_tests` and `wallet_tests` were adapted to AokChain, and the test fixture got a governance state, without which every check of a transaction's inputs crashed on a null pointer.)

| Failing suites | Reason (all inherited test data or assumptions) |
|---|---|
| `bloom_tests`, `merkleblock_tests`, `transaction_tests`, `coins_tests`, `sighash_tests`, `script_tests` | Transactions, blocks and coins serialized in the Bitcoin format. AokChain transactions have `nTime`/`nMessage` and coins have a time field, so deserialization goes wrong |
| `key_tests`, `rpc_tests` | Addresses and secret keys with Bitcoin/Ravencoin prefixes, assumptions of the Raven token RPCs |
| `pow_tests`, `versionbits_tests`, `policyestimator_tests` | Expect Bitcoin's difficulty, deployment and fee parameters |
| `tx_validationcache_tests` | `checkinputs_test` assumes Bitcoin's staged activation of script rules (blocks before DERSIG). AokChain enforces them from the start. The double-spend test passes |
| `miner_tests` | Its test cases are disabled |

These 13 do not point to defects in the code; the tests were never ported to AokChain. But **those areas (script vectors, sighash, transaction parsing) are not protected by unit tests**, and regenerating the vectors in the AokChain format is what remains (in this order: `tx_validationcache_tests`, `script_tests`, `sighash_tests`, `transaction_tests`).

| Status | Suite | Cause | Direction of the fix |
|---|---|---|---|
| ✖ 5 fail | `bloom_tests` (bloom_match, merkle_block_1/2/2_with_update_none/3_and_serialize) | They use serialized transaction and block hex and secret key strings in the Bitcoin format. AokChain transactions have `nTime`/`nMessage`, so deserialization fails | Create real transactions and blocks on regtest, extract the hex and replace the vectors. The test logic (filter matching) can stay as it is |

Main test bugs fixed this time (for reference; do not repeat the same mistakes):
- `vchBlockSig` was missing from `TestHeaderAndShortIDs`, so the compact block serialization was misaligned.
- The token tests used the protected name `"AOKCHAIN"` → the positive tests failed and the negative tests were passing for the wrong reason.
- The base58 vectors had Bitcoin/Raven prefixes → regenerated with `contrib/testgen/gen_base58_test_vectors.py` (Python 3).
  ```bash
  cd contrib/testgen
  python3 gen_base58_test_vectors.py valid 50   > ../../src/test/data/base58_keys_valid.json
  python3 gen_base58_test_vectors.py invalid 50 > ../../src/test/data/base58_keys_invalid.json
  ```
  When the chain prefixes are changed, change the constants at the top of this script as well.

## 2. Functional tests (`test/functional`)

In the original, a bulk replacement had turned `os.path.realpath` into `os.path.reaokath`, so **every test failed at the import stage**.
Current baseline (2026-11-11, default set with `-j8`): **38 pass / 47 fail (including variants) / 1 skipped**. A full run takes about 90 seconds.

**PoS testing:** regtest is PoW up to block 1,440,000, so staking could not be tested. When the regtest-only `-lastpowblock=<height>` lowers that height, a node with `-staking=1 -emergencystaking=1` produces a PoS block in every 16-second slot (`feature_staking.py` advances mocktime by 16 seconds at a time to move quickly). The difficulty is fixed at the minimum, so kernel competition and difficulty adjustment are not verified. Acceptance of mainnet PoS blocks is still checked with the verification node.

**Self-check of the wallet index:** the framework puts `checkwalletlive=1` into every node (document 08).

- The first baseline on 2026-11-09 had 23 tests. Adapting the Python-side transaction and block serialization to the AokChain format made 6 more pass (`128cf0f`), and 3 new tests were added (`feature_block_script_checks.py`, `feature_reindex_tokens.py`, `feature_governance_forged.py`).
- Python 3.12 needs the `python3-pyasyncore` package (`asyncore` was removed from the standard library).

- List of passing tests: `doc/aokchain/functional-baseline.txt` (for regression checks)

### Classification of the causes of failure
| Class | Typical symptom | Affected tests (examples) | Direction of the fix |
|---|---|---|---|
| A. Different economic parameters | `not(2100000196.0 == 125000)`, `getbalance() == 5000` | wallet_*, mempool_*, rpc_fundrawtransaction | regtest has a premine of 2.1 billion in block 1 and a reward of 4 AOK. Add a helper to test_framework that computes the expected values according to `GetBlockSubsidy` |
| B. Python transaction serialization | `TX decode failed (-22)`, `bad-txns-in-belowout` | rpc_rawtransaction, rpc_signrawtransaction etc. | **The framework side is done** (`128cf0f`). What remains is hex in the Bitcoin format and amounts hard-coded in individual tests |
| C. Leftover Raven names | `bip9_softforks['assets']`, `issue(asset_name=...)`, `listmyassets` | feature_assets*, feature_listmyassets, feature_unique_assets, feature_rawassettransactions | Change to the token RPC names and arguments (`token_name`); activation is height-based |
| D. Node fails to start | `aokchaind exited with status 1 during initialization`, `no RPC connection` | rpc_bind, feature_uacomment, wallet_multiwallet | Differences in error messages/options. Check individually |
| E. Disabled features | SegWit | feature_segwit, p2p_segwit (not in the default set) | Remove from the test list |

Recommended order: **A → C → D**, and replacing the hard-coded hex in individual tests.

## 3. Compiler warnings (gcc 13, 121 in a full build as of 2026-11-10; 119 as of 2026-11-11)

| Kind | Count | Assessment |
|---|---|---|
| `-Wrange-loop-construct` | 22 | Performance: `for (auto x : ...)` → `const auto&`. To be cleaned up in bulk |
| `-Wdangling-reference` | 22 | Mostly false positives on UniValue `find_value` |
| `-Wmaybe-uninitialized` | 11 | Need to be checked individually (one, `addressType`, was a real bug and has been fixed) |
| `-Wswitch` | 10 | Switches that do not handle the new `txnouttype` values (token/CLTV/offline) — state `default` explicitly after checking the behaviour |
| `-Wsign-compare` | 9 | To be cleaned up |
| `-Wdeprecated-declarations` | 9 | OpenSSL 3 / Boost API — to be resolved in Phase 2 |
| `-Wunused-variable` | 6 | To be cleaned up |
| `-Wreorder` | 6 | Harmless (`nTime`/`nLockTime`) |
| `-Wuninitialized` | 5 | `prevector` false positives |
| Others (`parentheses`, `misleading-indentation` etc.) | 5 | One `misleading-indentation` was a real bug and has been fixed |

Goal: 0 warnings → then CI with `--enable-werror`.

## 4. Runtime stability checks (checked = done, unchecked = still to do)

- [x] Full mainnet resync (`-par=1 -assumevalid=0 -txindex`) — about 4 hours, 2.5GB of block data. No violating block
- [x] Mainnet history scan (`contrib/aokchain/history_scan.py`) — about 11 minutes, 0 violations
- [x] `-reindex` works correctly — a full mainnet reindex with the final binary (`-par=8 -assumevalid=0`) passes and the result matches the live node. The original had 2 bugs that stopped the reindex (SEC-19, SEC-20)
- [x] `-reindex-chainstate` — confirmed with a regtest functional test (`feature_reindex_tokens.py`). Not done at mainnet scale
- [ ] Long-running testnet node (check memory usage and file descriptor leaks)
- [ ] Long-running staking node (wallet unlocked staking-only, offline staking)
- [ ] Unit and functional tests with an ASan/UBSan build — configure has no `--with-sanitizers` option, so specify `CXXFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined"` directly (or backport the upstream option). Detects UB such as SEC-02
- [ ] Fuzzing: `src/test/test_aokchain_fuzzy` is built (takes input on stdin). Add targets for the token script parser and compact block deserialization, then run with AFL/libFuzzer
