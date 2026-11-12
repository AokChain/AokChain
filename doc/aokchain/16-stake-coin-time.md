# 16. Coins that lose their time in a reorganization (stake blocks nobody accepts)

## What was wrong

A coin in the chain state records the time of its transaction (`Coin::nTime`) and whether
that transaction was a coinstake (`Coin::fCoinStake`). The undo data of a block
(`TxInUndoSerializer`) stores neither. When a block was disconnected in a reorganization,
the coins it had spent came back with `nTime = 0` and `fCoinStake = false`, and stayed
that way on that node.

The kernel hash of a stake contains the time of the staked transaction.

| Code | Time it used |
|---|---|
| `ConnectBlock()` (validation, 3.0.x) | the transaction, read from disk |
| `CheckKernel()` / `CheckProofOfStake()` (wallet staking thread and its self check) | `coin.nTime` |

For a damaged coin the staking thread therefore searched with time 0. It found kernels
that are valid only with time 0, built blocks from them and sent them out. Every node,
the staker's own included, rejected them in `ConnectBlock()` with `bad-cs-proofhash`. The
real kernels of that coin were never found, so the coin was never spent and never repaired
itself.

The typical way to get such a coin is a stake of one's own that loses against a competing
block: the orphaned block is disconnected and its kernel comes back without a time. The
damage therefore gathers on exactly the nodes that stake.

## Observed on the main network (2026-11-12)

- The 4.0 verification node rejected 583 blocks with `bad-cs-proofhash` in 2,819 heights
  (20.7 % of the heights, about 13 per hour). 3.0.x nodes relay them (compact block
  announcement before `ConnectBlock()`) and then drop them; the chain never split.
- Four such blocks were captured from 3.0.2 peers with their transactions. For all four the
  kernel hash meets the target with time 0 and does not meet it with the time of the staked
  transaction; for the seven accepted blocks captured in the same period it is the other
  way round. The staked coins were 37 to 219 days old and unspent.
- The rate means that stake weight of roughly a fifth of the weight that produces valid
  blocks sits on damaged coins and earns nothing.

## What 4.0 does

Nothing here changes which blocks are valid.

1. **Staking** (`pos.cpp`, `GetStakeCoinTime()`): for a coin without a time,
   `CheckKernel()` and `CheckProofOfStake()` take the time of the transaction
   (`ReadCoinTxInfo()`), as `ConnectBlock()` does. A damaged coin stakes valid blocks again.
   If the transaction cannot be read the coin is not staked; no block is built with time 0.
2. **Disconnecting a block** (`ApplyTxInUndo()`): a restored coin gets time and coinstake
   mark back from its transaction (transaction index; on a snapshot node from the record
   kept for coins from before the snapshot, see 15-snapshot.md). A node that reorganized
   now has the same chain state, field by field, as one that did not. The two
   `tx_verify.cpp` checks that use these fields (coinstake maturity, input time not after
   the spending transaction) were weaker on a damaged node; they are now as strict as on
   every node that did not reorganize, never stricter.
3. **Repair** (`repaircoins` RPC): walks the unspent outputs, and gives every one without
   a time the time and mark of its transaction. `repaircoins true` only counts.
   `-reindex-chainstate` has the same effect.

`-restorecointime=0` (regtest only) brings the old behaviour back for tests.
Test: `feature_stake_cointime.py`.

## For nodes that still run 3.0.x

`-reindex-chainstate` rebuilds the chain state with all times and repairs the coins until
the next orphaned stake. Upgrading to 4.0 and running `repaircoins` once is the lasting fix.

## Limits

- Needs the transaction index (on by default). Without it, or on a snapshot node for a
  coin from before the snapshot that has no kept record, the coin stays without a time and
  is not staked; `repaircoins` reports it as `unreadable`.
- On regtest every kernel meets the target, so the functional test shows that the
  transaction time is used and that the block is accepted, not that a main network kernel
  is found. The kernel computation is the one `ConnectBlock()` uses.
