# 08. Wallet performance — slowing down as transactions accumulate

## Symptoms and cause
When a wallet is used for a long time (a staking wallet in particular), balance queries, sending, the GUI and RPC get slower and slower, and the whole node becomes sluggish.

The cause is that the wallet **walks every transaction in `mapWallet` from the beginning on every call**. Historical transactions that have already been spent completely are examined every time as well.

| Path | Number of full scans | Notes |
|---|---|---|
| Balance functions such as `GetBalance` | 1 per function | Does not use the cache (`GetAvailableCredit(false)`) |
| `getwalletinfo`, GUI balance update | 6 or more | Runs on every block |
| Sending (`CreateTransaction`) | 2~3 | Balance check + coin selection |
| **Staking thread** | **4 every second** | `HaveAvailableCoinsForStaking` → `GetBalance` → `GetOfflineStakingBalance` → `SelectCoinsForStaking` |

The staking thread had two more problems (found in stage 2).
- **It spins without resting when there are no coins to stake.** If the wallet is unlocked but has no balance, or all coins are immature (60 blocks right after a stake), the loop repeats the scan without `sleep`, uses 100% of one core and keeps holding `cs_main`.
- **It searches again every second under the same conditions.** The result of the kernel search depends only on the tip and on the block time in 16-second units, yet a block template was built and the full search repeated every second.

One scan does, for every transaction, a lookup in `mapBlockIndex` (3 million entries on mainnet), lookups in `mapTxSpends` and `mapWallet`, and script interpretation (`IsMine`). All of this runs while holding `cs_main` and `cs_wallet`, so when the scan gets long, block validation, P2P and RPC stop along with it. A staking wallet gains one transaction per stake, so it is affected first.

## Measurements (regtest, wallet grown with self-sends, about 700 unspent outputs)

Before the fix — proportional to the number of transactions, and steeper as it grows because of cache misses.

| Wallet transactions | `getbalance` | `getwalletinfo` | `getstakinginfo` | `sendtoaddress` |
|---|---|---|---|---|
| 1,000 | 2 ms | 3 ms | 3 ms | 10 ms |
| 3,000 | 6 ms | 9 ms | 10 ms | 17 ms |
| 30,000 | 67 ms | 141 ms | 215 ms | 152 ms |

The same 30,000-transaction wallet before the fix, after stage 1 (`d5458c4`) and after stage 2 (`1d706e9`, `ed715ce`):

| Call | Before | Stage 1 | Stage 2 | Factor |
|---|---|---|---|---|
| `getbalance` | 67 ms | 2.1 ms | 2.4 ms | 28× |
| `getwalletinfo` | 141 ms | 5.5 ms | 5.4 ms | 26× |
| `getstakinginfo` | 215 ms | 5.2 ms | 7.2 ms | 30× |
| `listunspent` | 134 ms | 27 ms | 24 ms | 5× |
| `sendtoaddress` | 152 ms | 29 ms | 18 ms | 8× |
| `listaddressgroupings` | did not finish in over 11 minutes | (same) | 1.4 s | more than 450× |
| `listaddressgroupings` (3,000-transaction wallet) | 6.6 s | (same) | 0.19 s | 35× |
| `listtransactions "*" 10` | 1 ms | 1 ms | 1 ms | — |

CPU used by the staking thread (staking for 64 seconds with the same wallet; 4 blocks produced in all three cases):

| Situation | Before | Stage 1 | Stage 2 |
|---|---|---|---|
| Staking with the 30,000-transaction wallet | 26.5% of a core | 0.9% | 0.14% |
| Wallet with no coins to stake | 99.9% | 99.9% | 0% |

During this time the thread holds `cs_main` and `cs_wallet`, so block validation, P2P and RPC are blocked less by the amount saved. The 18 ms of `sendtoaddress` is at the same level as the values before the fix for the 1,000-transaction wallet (10 ms) and the 3,000-transaction wallet (17 ms); it is no longer proportional to the wallet history. Loading the wallet takes 0.75 seconds at 30,000 transactions (46 MB).

On mainnet, `mapBlockIndex` is much larger, so the figures before the fix are worse than these. After the fix the cost follows the **number of unspent outputs**, not the wallet history.

The measurement tool was not added to the repository. Method: on regtest, grow the wallet by producing 1 block for every 200 self-sends, call each RPC 3 times and take the minimum.

## What was changed (`src/wallet/wallet.{h,cpp}`, unrelated to consensus and P2P)

1. **Cache for the block depth lookup.** `CMerkleTx::GetDepthInMainChain` remembers the result of `mapBlockIndex.find(hashBlock)`. The key is `hashBlock`, so when the value changes it is looked up again automatically. Upstream solved the same problem by storing the block height in the transaction (bitcoin#15931).
2. **Determining a "fully spent transaction" (`CWalletTx::IsFullySpent`).** True when all outputs that are mine have been spent by transactions confirmed on the active chain. Such a transaction contributes nothing to the balance or to coin selection.
3. **Index of live transactions (`CWallet::GetLiveTxs`).** Collects only the transactions that are not fully spent, in the same order as `mapWallet`. The 10 balance functions, `AvailableCoinsAll` and `AvailableCoinsForStaking` iterate over this instead of the whole of `mapWallet`. This goes in the same direction as upstream making the wallet track its own outputs separately (bitcoin#27286).

Stage 2 (`1d706e9`, `ed715ce`, `a103a30`):

4. **Staking loop (`src/miner.cpp`).** Rests when there are no coins to stake. A search that found no kernel is not repeated until the tip or the block time in 16-second units changes. The kernel search (`CreateCoinStake`) runs while holding `cs_main` and `cs_wallet` — previously it read the UTXO cache (`pcoinsTip`) and the tip without locks and raced with the validation thread (SEC-24).
5. **Exclusion of orphaned stakes.** A coinbase or coinstake whose block is not on the active chain is treated as "immature, off chain" by all balance and coin selection functions and contributes 0. It is now removed from the index. Previously it stayed forever, and a staking wallet walked every stake that had lost the competition, every time.
6. **No full rebuild when a key is added.** Every send creates a key for the change address, and each time the whole index was rebuilt. For a key to turn an output into "mine", the script of that output must mention the key, so the "not mine" outputs of fully spent transactions are indexed by the key IDs that appear in their scripts, and only the transactions that mention the new key are looked at again.
7. **`listaddressgroupings` (`GetAddressGroupings`).** Merging by copying whole groups (proportional to the square of the size of the largest group) was replaced with union-find. The result is the same. The GUI coin control uses this path as well.
8. **`purgeorphanstakes` RPC.** Deletes orphaned coinstakes that the wallet has abandoned from the wallet file (default: those received more than a day ago). The balance does not change, and if the block is connected again they come back in from the block.
9. **Clean-up of `ZapSelectTx` (also used by `removeprunedfunds`).** Cleans up the `wtxOrdered` pointers and `mapTxSpends` entries that used to be left pointing at the deleted transaction (SEC-25).

Stage 3 — aligned with forms proven upstream (`04103d4`, `643fa84`). The fixes written from scratch in stage 2 were compared with the corresponding code in Bitcoin Core, Qtum, Blackcoin and Peercoin, and the following was changed.

| Item | Upstream | Adopted |
|---|---|---|
| Staking template | Qtum `ThreadStakeMiner`: searches for the kernel with an empty template (`CreateEmptyBlock`) and only after finding one builds a block filled with transactions and signs again | Changed to the same two-step structure. Previously a block was assembled from the mempool for every search and thrown away |
| `wtxOrdered` in `ZapSelectTx` | bitcoin#13437: stores an `m_it_wtxOrdered` iterator in the transaction and erases in O(1). Also prevents the duplicate insertion in `LoadToWallet` | Replaces the full-scan approach of stage 2 |
| `mapTxSpends` in `ZapSelectTx` | bitcoin#18850 added the clean-up but erased per output, which had the bug of also erasing the records of conflicting transactions; #34358 (2026-01) fixed it to erase "only the entries of the deleted transaction". `CT_DELETED` notification | The #34358 form (the stage 2 implementation was already the same) + the notification added |
| Disconnecting a stake block | Qtum `DisableTransaction`: finds the transaction of the staked output with `find` (`at` throws if it is not in the wallet) and calls `MarkDirty` on the related transactions | Adopted as it is |
| Orphaned stakes at startup | Qtum `CleanCoinStake`, Peercoin `ReacceptWalletTransactions`: at startup, mark my coinstakes that are not on the active chain as abandoned | `CWallet::CleanCoinStake()` added (skipped during reindex and import) |

What was deliberately left different from upstream:
- **Deleting orphaned stakes (`purgeorphanstakes`).** Qtum, Blackcoin and Peercoin all only mark orphaned stakes as abandoned and do not delete them from the wallet. So this RPC is kept only as a clean-up tool that the operator calls explicitly; it never runs automatically.
- **Locking during the kernel search.** Blackcoin searches while holding `cs_wallet`+`cs_main` (the same as this repository), and the latest Qtum caches the coin list per tip and searches without locks. The latter would require rewriting the staking code, so it was not adopted. The search runs once per tip and 16-second slot, so the lock time is short.
- **`listaddressgroupings`.** The latest Bitcoin Core still has the original quadratic algorithm, so there is no upstream fix to refer to. The union-find implementation is kept.
- Ravencoin had no fixes related to wallet performance, and the pointer problem in `ZapSelectTx` is still there as well.

Invalidation rules that keep the results the same as before the fix:

| Situation | Handling |
|---|---|
| A reorganization makes the spending transaction unconfirmed | If the tip at the time of the determination is not on the active chain, the determination is discarded and the index is rebuilt |
| The state of the spending transaction changes inside the wallet (conflict, abandon, delete) | The existing code calls `MarkDirty()` on the transactions on the input side. `MarkDirty()` clears the determination and puts the transaction back into the index |
| A key is added (change key of a send, HD derivation, `importprivkey`) | Only the transactions in which that key ID (or public key hash) appears in the script of a "not mine" output are checked again. Transactions with an output that carries the hash of a script the wallet knows (P2SH etc.) are checked again whenever a key is added |
| A script or watch-only entry is added/removed | Everything (`nKeyEpoch`): all outputs that were "not mine" at the time are checked again and the key index is rebuilt as well |
| The block of an orphaned coinbase or coinstake is connected again | When the wallet receives the block notification, it comes back through `MarkDirty()`. Those that still have their block hash are brought back as soon as that block enters the active chain, even before the notification |
| `zapwallettxes`, `removeprunedfunds`, `purgeorphanstakes` | The index is cleared and rebuilt. Transactions that were spent by the deleted transaction get `MarkDirty()` |

**Self-check option `-checkwalletlive`:** every time the index is used, it verifies exhaustively that the transactions left out of the index really contribute nothing when computed without caches, and aborts with an assert otherwise. It is slow and therefore meant for testing; the functional test framework turns it on in every test.

Not changed: `GetAddressBalances` (it has to output addresses with a balance of 0 too, so nothing can be skipped. It walks everything once and is included in the 1.4 seconds of `listaddressgroupings` at 30,000 transactions), `GetLegacyBalance` (`getbalance` with an account argument), the `listtransactions` family.

## Verification
- **Identical results:** on copies of the 30,000-transaction wallet, the balances, `getwalletinfo`, the whole of `listunspent 0` and the staking weight were dumped with the binary before the fix and with the stage 1 and stage 2 binaries and compared. They are byte-for-byte identical at all 6 points: initial state, 3-block reorganization, recovery, 20-block reorganization, recovery, coin locking. The result of `listaddressgroupings` is the same as before the fix on the 3,000-transaction wallet (at 30,000 it could not be compared because the binary before the fix does not finish).
- **Functional test baseline with `-checkwalletlive` on (34 tests at the time, 38 as of 2026-11-11):** passes while verifying the index exhaustively on every wallet call of every test (5 runs, 2 of them with the final build).
- **Regression test `wallet_spent_cache.py`:** for transactions remembered as fully spent, it checks that the coins show up again when (1) someone else's output becomes mine through a key import, (2) the spending transaction is pushed onto a conflicting chain, (3) an HD wallet derives the key of that output later (a wallet restored from a backup).
- **Regression test `feature_staking.py`:** enters the PoS phase with the regtest-only `-lastpowblock` and produces blocks with the real staking thread. It checks acceptance of the blocks by a peer, invalidation of a stake block on the same parent followed by recovery, a stake pushed out by a longer chain and the reorganization that brings it back, the same state after a wallet restart, and `purgeorphanstakes`.
- **Mutation testing:** removing the key-added notification makes `wallet_spent_cache.py` fail, and removing the `MarkDirty()` handling of coinstakes makes `feature_staking.py` fail (both are caught by `-checkwalletlive`). Removing the reorganization invalidation of stage 1 makes `wallet_spent_cache.py` fail.
- Unit tests: no change in the result (73/78 as of 2026-11-11; the 5 failures are the pre-existing `bloom_tests`).
- **GUI (`aokchain-qt`):** builds with Qt 5.15 (system packages). In headless mode (`QT_QPA_PLATFORM=offscreen`) the 30,000-transaction wallet was opened and RPC responded after 3 seconds; sending took 20~30 ms, and the GUI process used 0.1 seconds of CPU while receiving 5 blocks. Compilation of the locked balance cache fix (`463c3bd`) was also confirmed with this build. No test with the GUI on a real screen was done.
- **Mainnet test (verification node, 200 AOK of test coins, 2026-11-11):**
  - `-staking=1` with a wallet that has only immature coins: with the previous binary (`d5458c4`) the staking thread used 100% of a core (measured 3 times over 30-second intervals); with the new binary it was 0%. The busy loop of SEC-24 was reproduced exactly on mainnet and is fixed.
  - 2 transactions created by the 4.0.0 candidate (split into 10 outputs `4396386e…`, ordinary send `15e6e04d…`) propagated to 3.0.x peers and were confirmed in the next block.
  - The node runs with `checkwalletlive=1` and there are 0 index mismatches.
  - For the staking result see "Mainnet staking test" in document 01.
- **What could not be verified:**
  - There is no test that isolates only the path "an orphaned stake that still has its block hash comes back before the notification" (the same situation is covered jointly by the recomputation on leaving the tip and by `MarkDirty()`).
  - Handling of a stake that really lost the competition and became an orphan on mainnet. This was verified only on regtest.

## What remains

| Item | Status |
|---|---|
| The kernel search holds `cs_main` and `cs_wallet` | Once per tip and 16-second slot, searching with an empty template (stage 3). A lock-free search needs the structure of the latest Qtum (per-tip coin cache) |
| GUI transaction list | Loads all transactions into the model at startup. At 30,000 transactions startup takes 3 seconds, which is not a problem |
| Wallet file size | `purgeorphanstakes` deletes only orphaned stakes. Confirmed historical transactions stay (46 MB at 30,000 transactions, loading in 0.75 seconds, so not a bottleneck) |
| `GetLegacyBalance`, full `listtransactions` queries | Proportional to the history. The account feature has been removed upstream |

Interim workaround for operators (3.0.x): separate the staking wallet from the deposit/withdrawal wallet, and move the balance of a wallet with many accumulated transactions to a new wallet. On 3.0.x, avoid leaving a wallet with no balance, or with only immature coins, unlocked with `-staking=1`, because it keeps using one core.
