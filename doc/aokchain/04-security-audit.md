# 04. Security audit findings and remediation plan

- Audit date: 2026-11-09, target: the `modernize` branch (code based on the original `5d1fd3d`)
- Method: code review (checked in the build and unit test environment). **No actual attack was reproduced (no exploits).**
- Notation: **[confirmed]** = confirmed by tracing the code path directly / **[presumed]** = based on knowledge of public CVEs or on reading the code, not verified
- Status: `fixed` / `open` / `needs investigation`

> ⚠ **Fixes that affect the consensus rules must follow the "consensus change procedure" in 06-roadmap.md.**
> A wrong fix splits the node from the existing mainnet chain (chain split).

---

## Summary (in order of priority)

| ID | Severity | Item | Consensus impact | Status |
|---|---|---|---|---|
| SEC-01 | **Critical** | Result of script (signature) verification inside blocks ignored | Confirmed that no fork is needed | **fixed** (`0b23d4e`) |
| SEC-02 | **Critical** | Integer overflow in the sum of token transfers → tokens can be created | Confirmed that no activation height is needed | **fixed** (`a016189`) |
| SEC-03 | **Critical** | Remote node crash through a resent BLOCKTXN | No | **fixed** (`b809e5a`) |
| SEC-04 | High | Overflow in the sum of a token reissue | No activation height needed | **fixed** (`a016189`) |
| SEC-05 | **Critical** | Governance commands can be applied without a master key signature (in combination with SEC-01); governance state changes are applied even when block validation fails | No fork needed | **core path fixed** (`10a3fd7`) — the signature failure path is closed. The other failure paths (reward too high etc.) and `fJustCheck` remain |
| SEC-06 | High | PoS "fake stake" disk/memory exhaustion | No (policy) | **partly fixed** (`e5c3203`, `d4e3bb2`) — only blocks whose parent is on the active chain are checked in advance. Deep forks and header spam remain |
| SEC-07 | High | OpenSSL 1.0.1k (EOL) — used for the RNG | No | **partly fixed** (`1a11bda`) — depends raised to 3.0.15. Removing the OpenSSL dependency of the RNG remains |
| SEC-08 | Medium | `nBits` check skipped while syncing the first 1440 blocks | Yes | **Fixed** (rc7) — the difficulty field is checked for every proof-of-stake header. Blocks 1,443 to 3,031,432 all have the value of the formula (1441 and 1442 of the main network do not and are left out) |
| SEC-09 | Medium | Index access before the size check when parsing governance scripts | Same result | **fixed** (`10a3fd7`) |
| SEC-10 | Medium | A single master key can freeze arbitrary addresses | Policy | needs documentation |
| SEC-11 | Medium | No rate limit for addr messages, no defence against low-work header spam | No | **partly fixed** (`a641851`) — addr rate limit added. The header spam defence remains |
| SEC-12 | Medium | zeromq 4.2.2 (CVE-2019-6250 etc.) | No | **fixed** (`1a11bda`, 4.3.5) |
| SEC-13 | Medium | `fWalletUnlockStakingOnly` is a global flag (multi-wallet) | No | **Fixed** (rc7) — `fUnlockStakingOnly` is a member of each wallet |
| SEC-14 | Medium | `CheckAmountWithUnits` uses floating-point `pow()` | Same result | **fixed** (`a016189`) |
| SEC-15 | Medium | `MAX_MONEY = INT64_MAX` — the range check is effectively meaningless | Yes | **Fixed** (rc7) — `MAX_MONEY` is 25 billion AOK. With 2.1 billion coins in existence no block can be affected |
| SEC-16 | Low | `addressType` uninitialized (address index) | No | **fixed** (`b844eb7`) |
| SEC-17 | Low | Checkpoints/assumevalid/minimumChainWork need updating, one DNS seed | No | **Partly fixed** (rc7) — checkpoints up to 3,030,000, fixed seeds rebuilt from seven nodes that answer. The DNS seed `dns.aok.network` does not resolve |
| SEC-18 | Low | GUI dependencies (Qt 5.7.1, zlib 1.2.11, freetype 2.7.1, expat 2.2.1) | No | **partly fixed** (`f8cdd48`) — Qt 5.15.10 for the Windows build. The Qt depends for Linux remains |
| SEC-19 | High | `-reindex-chainstate` does not wipe the token and governance databases and marks a valid block (179,185) invalid | No | **fixed** (`25c7554`) |
| SEC-20 | High | `-reindex` cannot index blocks stored before their parent and stops midway (at 3,440 with mainnet data) | No | **fixed** (`1d870fb`) |
| SEC-21 | Low | The check that the AOK amount of a token output is 0 is skipped when there is no `tokenCache`. The review showed that the block and mempool paths always pass a cache, so this path was not reachable in consensus (defensive fix) | No impact | **fixed** (`d5ccfef`) |
| SEC-22 | Medium | A mutated block (merkle/signature mismatch) resets the download state of another peer | No | **fixed** (`a641851`) |
| SEC-23 | Medium | `ActivateBestChain` terminates with a null dereference when it receives a block without a hash pointer. Happens when a peer's block request (getdata, GETBLOCKTXN) arrives right after another thread (staking, `submitblock`) accepted a block. Present in the original 3.0.x code | No | **fixed** (`1d028b4`) |
| SEC-24 | Medium | Staking thread: (1) when there are no coins to stake it scans the wallet repeatedly without `sleep` while holding `cs_main` and `cs_wallet`, uses 100% of one core and starves validation, P2P and RPC. (2) The kernel search reads the tip and the UTXO cache (`pcoinsTip`) without locks and races with the validation thread (found by code review; a crash was not reproduced). Present in the original 3.0.x code. (1) was reproduced on the mainnet verification node (core 100% → 0%) | No | **fixed** (`1d706e9`, `04103d4`) |
| SEC-25 | Low | `ZapSelectTx` (`removeprunedfunds`) leaves behind the `wtxOrdered` pointer of the deleted wallet transaction (later access to freed memory in `listtransactions`) and its `mapTxSpends` entries (if another transaction spends the same output, termination by the assert in `MarkConflicted` — reproduced while testing the new RPC). Reachable only through local RPC. Bitcoin Core fixed it with #13437, #18850 and #34358, and the code was brought into the same form | No | **fixed** (`ed715ce`, `643fa84`) |
| SEC-26 | **High** | Offline staking: the holder of a staking key could put delegated coins behind a kernel of his own in a coinstake and pay them to himself, or pay back less than the stake took in. Validation looked only at the script of the kernel input, not at the other inputs or the amounts | Tightening of the rules. No activation height needed: the main network never had an offline staking output (all blocks and the chain state were searched) | **Fixed** (rc7) — `CheckOfflineStakeScripts()`, 18-offline-staking.md |
| SEC-27 | Medium | The message of a version 2 transaction (`nMessage`) is not part of what is signed. Anyone on the relay path can change the message of an unconfirmed transaction, which changes its txid as well; the signature stays valid | Needs a hard fork (with the message in the signature hash, old nodes refuse the new signatures) | **Not fixed** — the wallet uses version 2 only when a message is given, so ordinary payments (version 1) are not affected. Do not use the message to identify deposits. `txmessage_tests` records the present behaviour |
| SEC-28 | Medium | One stake (a coin and a time) can sign any number of different blocks, and each passes every check made before a block is stored, so a staker could make a node store blocks without end (filling the disk) | No | **Fixed**: four blocks with the same stake are stored (`-maxblocksperstake`). A further one is stored when a header builds on it, so the node follows the chain whichever block it started with. No consensus rule changes. `feature_stake_variants.py` |
| SEC-29 | High | `walletpassphrase` checked its arguments **after** it had opened the wallet. A call with the right passphrase and a bad argument (negative timeout, non-numeric timeout, non-boolean third argument) failed with an error but left the wallet open for payments without a time limit. Also, the mark "back to staking only after a timed unlock" was shared by all wallets, so the timer of another wallet could use it up. (Found in the external review of 2026-11-13) | No | **Fixed** (4.0.1): the arguments are checked first and the mark is per wallet. `wallet_encryption.py` |
| SEC-30 | High | Governance commands (freeze, unfreeze, issuing cost, fee address) were written to the governance database **while** a block was being checked. When the block was refused after that (a coinbase that pays too much, a failing script), when a block was only checked and not connected (`TestBlockValidity`: block proposals, proof-of-work block templates), and when the last blocks were checked at startup (`-checklevel=3`: undone and not applied again), the state still changed, and the node then refused valid blocks (a chain split). An attack needs a master key transaction that is not in a block yet and some stake. The replay after an interrupted flush (`ReplayBlocks`) undid commands a second time. (Found in the external review of 2026-11-13; the template, startup check and replay paths were found while checking it) | No (the result of a valid block is the same) | **Fixed** (4.0.1): the commands of a block go into a level of changes that later transactions of the same block see at once, and are written in one batch only when the block is valid to the end and is connected. The startup check opens an outer level and discards it. `feature_governance.py`, `governance_tests` |

---

## Summary of the verification results of 2026-11-10

- **Full mainnet sync** (`-par=1 -assumevalid=0`, 3,030,485 blocks): passed without stopping. The historical main chain contains no block that fails signature verification, so SEC-01 was restored without a fork.
- **Full revalidation with the final binary** (`-reindex -par=8 -assumevalid=0`, 3,031,060 blocks): 0 invalid. Block hash, token list and governance state match the live node.
- **History scan** (`contrib/aokchain/history_scan.py`, up to height 3,031,015): 0 violations of the candidate rules (S-01~S-05, S-10) in 3,029,575 PoS blocks and 972,292 token outputs. A reissue amount of 0 is normal (only units or IPFS changed) and must be allowed.
- **Reproduction tests:** tests confirmed that the code before the fixes (a) accepts a block with a wrong signature under `-par=4`, (b) creates tokens with outputs of `INT64_MAX + INT64_MAX + 1002`, and (c) stops after `-reindex-chainstate`; after the fixes they all pass.
- **Live observation:** PoS blocks with a wrong kernel hash (side branches of length 1) keep being broadcast on mainnet. About 50 in 4 hours.

### Governance forgery (SEC-01 + SEC-05) — applies to the current 3.0.x network
- Governance commands are carried in a transaction that spends an output of the master address. Anyone can create such an output by sending coins to the master address.
- A 3.0.x multi-core node does not check the result of signature verification inside blocks, so it **accepts a block that "spends" that output with a bogus signature and carries commands to freeze, change costs or change the fee address.** Anyone who can produce a block by staking can freeze arbitrary addresses without the master key.
- 4.0.0 verifies the signature of a governance transaction directly, before applying it (`10a3fd7`). The regression test is `feature_governance_forged.py`.
- To be checked by the operators: compare `freezelist` and `issuanceinfo` to see whether there were governance changes in the past that were not issued by the holder of the master key. When the whole of mainnet was validated with `-par=1 -assumevalid=0`, every signature passed, so the historical main chain contains no governance transaction with a wrong signature. Whether those changes match what the operators intended still has to be compared separately.

### Independent code review (2026-11-10)
A separate reviewer read and reviewed all of these changes. No change was found that makes the node reject valid blocks, and the following remarks were incorporated: signature verification before applying governance (`10a3fd7`), rewriting the token undo data after `-reindex-chainstate`, cleaning up in-flight state when handling a mutated block, handling of BLOCKTXN failures, the allowance for getaddr responses, the reissue comparison (`1fbbd9c`).

**Note for deployment (review remark 2):** the SEC-01 and SEC-02 fixes are "rule tightenings that take effect immediately", without an activation height. If someone creates a block that is valid only on 3.0.x (a block with a wrong signature, a transaction whose token sum overflows), 3.0.x nodes accept it, 4.0.0 nodes reject it, and they split. The correct side is then 4.0.0, but **until a majority of the staking weight has moved to 4.0.0, the 4.0.0 nodes can be left on the minority chain.** Therefore, agree the upgrade schedule with the major stakers and exchanges before the release.

### Lesson: regression in the advance check (must read)
The fake stake advance check of `e5c3203` at first also checked fork blocks whose parent had not been connected yet. The kernel hash uses `pprev->nStakeModifier`, and this value is computed only in `ConnectBlock`. So it was computed with a modifier of 0, which **marked the valid mainnet block 3,030,580 invalid, and the node could not follow the chain.** It was found on the verification node and fixed in `d4e3bb2` (check only when the parent is on the active chain; when uncertain, store as before).
- When adding a check to the block acceptance path, first verify whether "the same state as in `ConnectBlock` (stake modifier, UTXO) is available at that point".
- Such changes are not verified by regtest (regtest PoS, available through `-lastpowblock`, runs at a fixed minimum difficulty and does not stand in for mainnet PoS blocks). Release only **after confirming on the mainnet verification node that it follows the tip for several days or more**.
- Using `submitblock` on a node whose reindex has not finished overwrites the block files. Do not do this while debugging.

## SEC-01 [confirmed] Result of script verification inside blocks is ignored — Critical

> **Status: fixed (`0b23d4e`).** Step 1 of "How to fix" below (checking the history) was carried out; there was no violating block, so the check was restored as it was. The regression test is `feature_block_script_checks.py`.

- Location: ConnectBlock in `src/validation.cpp`, `// if (!control.Wait())` (about L2917)
- Introduced: commented out in commit `290f3c1` "Tokens hardfork" (2020-09-20).
- Behaviour:
  - `-par` defaults to 0 → with 2 or more cores, `nScriptCheckThreads > 0` (`init.cpp`).
  - In that case `CheckInputs` only queues the script checks, and the result is checked only through `control.Wait()`.
  - The destructor `~CCheckQueueControl` also calls `Wait()` but **discards the return value** (`checkqueue.h`).
  - → A multi-core node **accepts a block that contains a transaction with a wrong signature**. A staker can also use someone else's UTXO in a coinstake.
  - A single-core (`-par=1`) node verifies directly and rejects → there is also a risk of a split between nodes.
  - Below `assumevalid` (block 550000 as recorded at the audit on 2026-11-09; moved to block 3,030,000 in `13222ef`) script verification is skipped anyway, so the range actually affected is the blocks after that.
- The mempool (`AcceptToMemoryPool`) does not use the queue and verifies directly, so **ordinary relayed transactions are verified**. The problem is transactions that a block producer inserts directly.
- **How to fix (simply removing the comment is not enough):**
  1. Validate a fully synced mainnet node from the beginning with `-par=1 -assumevalid=0` to check whether there has ever been a block that fails script verification.
     - None → remove the comment (compatible with the existing chain, effectively a bug fix).
     - Some → set an **activation height (hardfork height)** above that block and check only from there on. Also investigate the history of the original developers as to why it was commented out (whether it was a problem with a specific block at the time of the token hard fork).
  2. Regression test: add a functional test that a block containing an invalid signature is rejected under both `-par=1` and `-par=4`.

## SEC-02 [confirmed] Overflow in the sum of token transfers — Critical

> **Status: fixed (`a016189`).** 0 violations in the history scan. Unit test `token_tx_overflow_test`.

- Location: `CheckTxTokens` in `src/consensus/tx_verify.cpp` — `totalInputs.at(name) += nAmount`, `totalOutputs.at(name) += transfer.nAmount` (no range check).
- `CTokenTransfer::IsValid` (`tokens/tokens.cpp`) checks only `nAmount > 0`; there is no upper bound (`MAX_MONEY_TOKENS`).
- Scenario: with output amounts `INT64_MAX`, `INT64_MAX`, `X+2` the sum wraps around and equals the input `X` → tokens that did not exist are created. (Signed overflow is UB in C++.)
- Fix (rule tightening = soft fork):
  - Reject `transfer.nAmount > MAX_MONEY_TOKENS`, check `0 <= sum <= MAX_MONEY_TOKENS` after every addition.
  - First scan the historical chain for transactions that violate this condition (can be checked with the same full validation node as for SEC-01).
  - Unit test: add overflow cases to `token_tx_tests`.

## SEC-03 [confirmed] Remote crash through a resent BLOCKTXN — Critical — fixed

- Location: BLOCKTXN handling in `src/net_processing.cpp`, `FillBlock` in `src/blockencodings.cpp`.
- On `READ_STATUS_FAILED` the block stays in flight while `FillBlock` has set the header to null → on the second BLOCKTXN, `assert(!header.IsNull())` → the process terminates. The same pattern as Bitcoin Core CVE-2024-35202.
- Fix (`b809e5a`): on FAILED, `MarkBlockAsReceived` and then getdata; if the header is already null, ignore the message. Only P2P handling changes, no consensus impact.
- Follow-up: add a functional test for the compact block resend scenario (possible after the Python-side transaction serialization is fixed, see document 05).

## SEC-04 [confirmed] Overflow in the sum of a token reissue — High
- Location: `CReissueToken::IsValid` in `tokens/tokens.cpp` — the comparison `token.nAmount + this->nAmount > MAX_MONEY_TOKENS` can itself overflow, and the `nAmount < 0` check comes after it.
- Fix: check `nAmount < 0` first, compare in the form `nAmount > MAX_MONEY_TOKENS - token.nAmount`. Checks were also added where the supply is summed.

## SEC-05 [confirmed] Governance state is written before block validation is complete — Critical

> **Status: core path fixed (`10a3fd7`).** The signature of a governance transaction is verified before it is applied. The remaining failure paths and `fJustCheck`, described below, are still open.

- Location: governance handling inside ConnectBlock in `validation.cpp` (about L2779-2870), **before** the `fJustCheck` return and the coinstake reward check.
- Impact: freezes and cost changes can be written to the database even for `TestBlockValidity` (getblocktemplate / staking template check) or for a block that fails later → state mismatch between nodes.
- Fix: collect the governance actions and apply them only after all validation has passed and only when `!fJustCheck`. The write path of the governance database needs detailed verification.

## SEC-06 [confirmed] PoS fake stake resource exhaustion — High
- Kernel and stake input verification happens only in `ConnectBlock`. `AcceptBlockHeader`/`AcceptBlock` check only the time, `nBits` and the block signature and then call `WriteBlockToDisk`.
- The block signing key comes from a coinstake output chosen by the attacker, so fake blocks pass the signature check as well → fake headers/blocks can be mass-produced at no cost on forks within the maximum reorg depth (500) (the family of the 2019 "fake stake" attacks).
- Fix: at the `AcceptBlock` stage, before storing, run `CheckProofOfStake`/the kernel check and check that the stake prevout exists; a duplicate stake limit in the style of `setStakeSeen`; a per-peer header/block index budget.

## SEC-07 [confirmed/presumed] OpenSSL 1.0.1k — High
- `depends/packages/openssl.mk` 1.0.1k (EOL, many CVEs).
- Besides Qt payment requests it is used in `random.cpp` (`RAND_bytes`, `RAND_add`) and `util.cpp` (`RAND_screen`/`RAND_cleanup`) → it is mixed into the random number path used for key generation.
- The build on the build server uses the system OpenSSL 3.0.13 (compiles OK).
- Fix: the proper way is to remove the OpenSSL dependency by porting `random.cpp` from upstream 0.20+. In the short term, move depends to OpenSSL 3.x.

## SEC-08 [confirmed] `nBits` check skipped in the initial PoW phase — Medium
- `ContextualCheckBlockHeader` skips the `nBits` check when `chainActive.Height() <= nLastPOWBlock` → the range of the first 1440 blocks during the initial sync.
- Fix: check unconditionally. (Needs confirmation that the existing chain passes — a consensus change.)

## SEC-09 [confirmed] Out-of-range read of governance scripts — Medium
- `validation.cpp` about L2798 / L2108: `out.scriptPubKey[0]` is read before the size is checked. Reached only in transactions that spend from the master address.
- Fix: check the size first. If the result changes it can affect consensus, so check for regressions against historical governance transactions.

## SEC-10 Policy: governance master key — Medium
- The single key of the mainnet master address `kp5eNwMzDfDgHXMG3XZw73c6rTA6WzFJEx` (chainparams.cpp) can freeze arbitrary scripts (`CanSend`).
- Fix: document the key custody/operating policy; in the long term consider multisig or timelocked governance.

## SEC-11 [confirmed] Missing P2P DoS defences — Medium
- Present: the CVE-2018-17144 duplicate input check (always applied to blocks), `MAX_INV_SZ`, the INVDoS limit, orphan transaction cap/expiry, the limit of 1000 addr entries.
- Missing: addr rate limit (token bucket), pre-synchronization of low-work headers (headers presync), no `Misbehaving` call in `ProcessNewBlock` for a wrong signature encoding.
- Fix: backport the upstream addr token bucket, a per-peer header budget (together with SEC-06).

## SEC-12 zeromq 4.2.2 — Medium [presumed]
- CVE-2019-6250, CVE-2019-13132. Applies only when `-zmqpub*` is enabled and exposed externally. Fix: 4.3.5 or later.

## SEC-13 [confirmed] The staking-only unlock flag is global — Medium
- `fWalletUnlockStakingOnly` in `wallet/wallet.cpp` is global. With multiple wallets, the setting of one wallet affects the others. Fix: make it a member of `CWallet`.

## SEC-14 [confirmed] Floating point in `CheckAmountWithUnits` — Medium
- `tokens/tokens.cpp` — uses `pow()`, does not check the range of `nUnits` of a transfer. Differences in the result between platforms can cause a consensus split. Fix: an integer lookup table (replace it after testing over the whole range that the result is identical).

## SEC-15 `MAX_MONEY = INT64_MAX` — Medium
- `amount.h`. `MoneyRange` and `bad-txns-vout-toolarge` are effectively powerless. The structure relies on checking the AOK sum after every addition. Fix: consider changing it to the real supply cap (a consensus change) or introduce overflow-checked addition.

## SEC-16 `addressType` uninitialized — fixed (`b844eb7`)

## SEC-17 Operation of the chain parameters — Low
- Last checkpoint 860000 (before governance activation at 871500), `defaultAssumeValid` 550000, `chainTxData` 2022-03 (as of the audit on 2026-11-09; assumevalid, minimumChainWork and chainTxData were updated to block 3,030,000 in `13222ef`).
- One DNS seed `dns.aok.network` + 11 fixed IPv4 addresses (3 of them responded on 2026-11-11, see 01-status.md).
- Fix: update checkpoints/assumevalid/minimumChainWork with every release, diversify the seeds.

## SEC-18 GUI dependencies — Low
- Qt 5.7.1, zlib 1.2.11 (CVE-2018-25032, CVE-2022-37434), freetype 2.7.1 (CVE-2020-15999), expat 2.2.1, protobuf 2.6.1, dbus 1.10.18 (as of the audit on 2026-11-09). Qt was updated to 5.15.10 for the Windows build (`f8cdd48`); the other packages were not re-checked.

---

## False positives / notes (judged to need no action)
- RPC defaults: binds to loopback, `-rpcallowip` is required, the authentication comparison is timing-safe → same as upstream.
- The `-Wuninitialized` warnings are gcc false positives in the `prevector` union move code, `-Wdangling-reference` is a false positive on UniValue `find_value`, `-Wreorder` is the order of `nTime`/`nLockTime` (harmless).
- `strncpy` (protocol.cpp) is an intended use. No `strcpy`/`sprintf`/`gets` (excluding third-party code).
