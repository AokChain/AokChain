# 07. List of upstream changes to adopt (Bitcoin Core · Ravencoin · PoS chains)

- Written: 2026-11-10. Target: the `modernize` branch
- AokChain is based on Bitcoin Core 0.16/0.17 + Ravencoin code from 2019~2020. Upstream security patches and features after that have not been adopted.
- Notation
  - **[V]** = verified directly in the AokChain code
  - **[U]** = unverified. External source or presumption
  - **[source]** = CVE numbers and dates collected from the web during research. Items from 2025~2026 could not be verified directly, so be sure to check the original advisory and PR before backporting
- Fork classification
  - **None** = only P2P/RPC/wallet/build change. An ordinary upgrade that can run mixed with old versions
  - **Policy** = mempool/relay policy. Block validity is unchanged
  - **Soft fork** = rule tightening. Needs an activation height
  - **Hard fork** = rule relaxation or change. All nodes and exchanges must upgrade
- Priority: P0 (immediately) → P3 (optional)

> **Principle (set by the maintainers):** do what is possible without a hard fork first. A hard fork only when the benefit is large, and on the premise that node operators and exchanges are persuaded.

---

## A. Possible without a hard fork (ordinary upgrade)

### A-1. P2P / DoS security patches (Bitcoin Core advisories)

| ID | Item | Upstream | AokChain | Priority | Size | Status |
|---|---|---|---|---|---|---|
| U-01 | Crash on a resent BLOCKTXN | CVE-2024-35202, 25.0 | [V] affected → **fixed** (SEC-03, `b809e5a`) | P0 | S | ✅ |
| U-02 | Script check queue and lifetime of `txdata` (UAF) | CVE-2024-52911 [source], PR #31112 | [V] affected → **fixed** (`0b23d4e`) | P0 | S | ✅ |
| U-03 | Double loop in orphan transaction handling | CVE-2024-52914, 0.18, PR #15644 | [V] `vWorkQueue` is processed to the end inside the TX handler | P1 | M | ⏳ |
| U-04 | A huge INV triggers one GETHEADERS per entry | CVE-2024-52915, 0.20, PR #18962 | [V] affected → **fixed** (`1c25384`) | P1 | S | ✅ |
| U-05 | A mutated block resets the download state of another peer | CVE-2024-52921, 25.0, PR #27608 | [V] A BLOCK from any peer calls `MarkBlockAsReceived`. More dangerous because a mutation that changes only `vchBlockSig` is free → **fixed** (`a641851`) | P1 | M | ✅ |
| U-06 | Header memory DoS (presync) | CVE-2019-25220, 24.0.1 | [V] affected. Under PoS a defence based on "work" is weak, so it is addressed with SEC-06 (kernel check before storing) and a per-peer header budget | P1 | L | ⏳ |
| U-07 | addrman `nIdCount` overflow, addr spam | CVE-2024-52919 [source], 22.0, PR #22387 | [V] addr token bucket **added** (`a641851`). Widening the type of `nIdCount` remains | P1 | M | partly ✅ |
| U-08 | Unbounded banlist | CVE-2020-14198, 0.20.1, PR #19219 | [V] Automatic ban of misbehaving peers → switch to "discourage" | P1 | M | ⏳ |
| U-09 | Peer time offset overflow | CVE-2024-52912, 0.21 | [V] `nTime - GetTime()` has no range limit. More important under PoS because the adjusted time is used in the kernel search → **fixed** (`1c25384`) | P1 | S | ✅ |
| U-10 | Compact block stalling | CVE-2024-52922 [source], 25.1/26.0, PR #27626 | [V] No limit on the number of concurrent compact block requests per block | P2 | M | ⏳ |
| U-11 | DoS through sorting of the inv-to-send set | 25.0, PR #27610 | [V] `setInventoryTxToSend` | P2 | S | ⏳ |
| U-12 | Censorship of transaction re-requests | CVE-2024-52913, 0.21 (`TxRequestTracker`) | [V] uses `mapAlreadyAskedFor` | P2 | L | ⏳ |
| U-13 | Cap on GETDATA processing | CVE-2024-52920, 0.20 | [V/U] Not an infinite loop. Serves an unlimited number of blocks per request | P3 | S | ⏳ |
| U-14 | Filling the disk with logs | CVE-2025-54604/54605 [source], PR #32604 | [U] No log rate limit | P2 | S | ⏳ |
| U-15 | Legacy sighash CPU DoS | CVE-2025-46598 [source] | [U] | P2 | M | investigate |
| U-16 | Undo file 4GiB position overflow | advisory id unverified [source] | [V] 32-bit `nUndoPos` | P3 | S | ⏳ |

### A-2. PoS-specific defences (BlackCoin / PIVX / Qtum / Particl)

| ID | Item | AokChain | Priority | Size | Status |
|---|---|---|---|---|---|
| P-01 | **Fake stake defence**: check the kernel, stake prevout and signature **before** a block is stored on disk | [V] Blocks whose parent is on the active chain are checked before they are stored (`e5c3203`, `d4e3bb2`). See the "Lesson" in document 04 | **P0** | M | partly ✅ |
| P-02 | Budget limit for fake stakes that use an already spent kernel (fork blocks) | [V] The kernel is checked only against the UTXO tip | P1 | M | ⏳ |
| P-03 | Blocking duplicate stakes (`setStakeSeen`) | [V] absent | P1 | S | ⏳ |

### A-3. Policy / defaults / feature removal

| ID | Item | AokChain | Priority | Size | Notes |
|---|---|---|---|---|---|
| C-01 | Turn the BIP37 bloom filter default off (`-peerbloomfilters=0`) | [V] `DEFAULT_PEERBLOOMFILTERS = true` | P1 | S | ⚠ **First check** whether AokChain SPV/mobile wallets depend on BIP37 |
| C-02 | Tor v3 (ED25519-V3) | [V] Uses `NEW:RSA1024`. Tor v2 was shut down in 2021, so the hidden service does not work | P1 | S | Spreading v3 addresses needs C-03 (addrv2) |
| C-03 | addrv2 (BIP155) | [V] absent | P1 | M | Only adds a P2P message. I2P/CJDNS are optional |
| C-04 | Remove UPnP (miniupnpc) | Removed upstream in 29.0. CVE-2024-52917 | P1 | S | For now only API 18 compatibility was added (`938aec5`) |
| C-05 | Remove BIP70 payment requests (GUI) | CVE-2024-52918, `qt/paymentserver.cpp` | P2 | S | Open; the GUI build is active again and still links BIP70 |
| C-06 | Block-relay-only outbound peers, anchors | [V] absent | P2 | M | Defence against eclipse attacks |
| C-07 | `getblock` verbosity 3, REST improvements | [V] Only verbosity 0~2 supported | P3 | S | Useful for exchanges and indexers |
| C-08 | full-RBF, package relay | Little effect at the current traffic | P3 | M | |

### A-4. Base technology / dependencies

| ID | Item | Priority | Size | Notes |
|---|---|---|---|---|
| T-01 | Random number generator without OpenSSL (`random.cpp`, 0.20) | P1 | M | SEC-07 |
| T-02 | Update depends: Boost, libevent 2.1.12, zeromq 4.3.5, OpenSSL 3 — **done** (`1a11bda`). GUI packages such as zlib and expat remain | P1 | M | ✅ (except the GUI) |
| T-03 | Update libsecp256k1 (constant-time fixes) | P2 | S | Schnorr is not needed |
| T-04 | Replace Boost threads with `std::thread`, switch to CMake | P2 | L | |
| T-05 | Fuzzing infrastructure (`test/fuzz`): token script parser and governance parser first | P2 | M | |
| T-06 | Functional test framework `asyncore` → `asyncio` | P2 | M | For now handled temporarily with the `python3-pyasyncore` package |
| T-07 | Descriptor wallets, SQLite, removal of BDB | P2 | XL | A large task that needs descriptor designs for tokens, offline staking and CLTV |
| T-08 | BIP324 v2 encrypted transport | P3 | L | |
| T-09 | BIP157/158 compact filters | P3 | M | Only if there are plans for a light client |
| T-10 | assumeutxo | — | XL | Not recommended because it needs snapshots of tokens, governance and the stake modifier |

---

## B. Soft fork bundle (recommended: once, height-based activation)

Ravencoin attempted the same kind of fixes with BIP9 voting (70%), but they were never activated. **Height-based activation** is recommended (the Avian way). Before including them, scan the history with a node that validates the whole of mainnet to check that the existing blocks all satisfy the new rules (document 06, section 1).

| ID | Item | Rationale | History scan |
|---|---|---|---|
| S-01 | **Block the overflow in the sum of token transfers** (SEC-02) — ✅ **included in 4.0.0** (`a016189`), no activation height | The same kind of bug as the one actually exploited on Ravencoin [source: Raven PR #1287, v4.7/4.8] | Done: 0 violations |
| S-02 | Block the token reissue overflow (SEC-04) — ✅ included in 4.0.0 | | Done: 0 violations |
| S-03 | Run the check that a token output must have `nValue == 0` unconditionally | [V] In `tx_verify.cpp` it runs only when `if (tokenCache)`, and there is also a `!fReindex` exception. The same type as the 2020 Ravencoin inflation (about 300 million RVN). ✅ included in 4.0.0 (`d5ccfef`) | Done: 0 violations |
| S-04 | Integer version of `CheckAmountWithUnits` (SEC-14) — ✅ included in 4.0.0 | Floating point can give different results on different platforms | Done: 0 violations |
| S-05 | Enforce the coinstake timestamp mask (`nTime & 15 == 0`) | [V] Stakers apply the mask but consensus does not check it. **Code ready, activation height undecided** (`d5ccfef`) | Done: all 3.02 million PoS blocks satisfy it |
| S-06 | Reduce FutureDrift (10 minutes → tens of seconds), reduce `-maxtimeadjustment` | [V] 10 minutes in `policy.cpp`. BlackCoin v3 uses 15 seconds | Applies only to new blocks |
| S-07 | `nBits` check in the initial PoW phase (SEC-08) | | Required |
| S-08 | Size check of governance scripts (SEC-09) | | Check the governance transactions |
| S-09 | (optional) Realistic `MAX_MONEY` (SEC-15) | | Check the total supply |
| S-10 | DoS 100 when a coinstake contains tokens — ✅ included in 4.0.0 (only the penalty changed) | [V] Previously `DoS(0)` | Done: 0 violations |

**Assessment:** this bundle blocks inflation bugs that have actually been exploited. There is ample justification for asking exchanges and operators to upgrade. The point "only block validity is tightened, user-facing features stay the same" can be used to persuade them.

---

## C. Hard fork candidates (benefit against cost)

| ID | Item | Benefit | Cost | Assessment |
|---|---|---|---|---|
| H-01 | **SEC-01 restore script verification** — ✅ **settled: no hard fork needed, included in 4.0.0** | Removes a critical defect | Ordinary upgrade | 0 violating blocks in the full mainnet validation |
| H-02 | Governance multisig/timelock (SEC-10) | Removes the risk that a single key can freeze assets | Needs explaining to exchanges | Recommended to include together if H-01 becomes a hard fork |
| H-03 | Improved stake modifier (v4 kernel family) | Less grinding | Large task | Not recommended at the current scale |
| H-04 | SegWit / Taproot | Fixes malleability, Lightning | Entangled with `nTime`/`nMessage`/token scripts, so the work is very large. Wallets and exchanges have to change | **Not recommended** |
| H-05 | Larger block size | None (no demand) | | **Not recommended** |
| H-06 | Ravencoin restricted assets, qualifiers, messaging | More features | New script types, XL | Review separately if a demand arises. Reward distribution (`distributereward`) is possible with RPC alone (no fork needed) |

---

## D. Recommended order

> **Current position (2026-11-10):** the 4.0.0 release candidate contains U-01/02/04/05/07 (partly)/09, P-01 (partly) and T-02 from section A, S-01~S-04 and S-10 from section B, and H-01 (settled as needing no fork). What remains is the rest of the items in step 1 below and the agreement on the activation height for S-05~S-08.

1. **Release 1 (no fork, ordinary upgrade)**
   - P-01~03 fake stake defence
   - Backports U-02~U-09
   - C-02/C-03 Tor v3 and addrv2
   - C-04 remove UPnP
   - T-01/T-02 random number generator without OpenSSL and dependency updates
   - Update checkpoints and assumevalid
   - Decide C-01 after checking the SPV dependency
2. **Check the result of the full mainnet validation:** history scan for SEC-01 (H-01) and S-01~S-08
3. **Release 2 (soft fork bundle B, height-based)**
   - If H-01 passes without a violating block, include it here
4. **(Only if necessary) hard fork:** if H-01 has violating blocks. In that case include H-02 as well
5. Other features (Phase 6) proceed after the requirements have been settled

## E. What could not be verified
- The exact code that caused the 2020 Ravencoin inflation bug, the changes in Ravencoin v4.3.x
- Whether the BlackCoin family was on the public 2019 Fake Stake disclosure list
- Details of the CVEs from 2025~2026 (the numbers are copied as they were found in the researched sources)
- Whether the AokChain ecosystem (mobile wallet, exchanges) depends on BIP37, Tor or UPnP. **Needs to be confirmed with the operators**
