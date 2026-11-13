# 18. Offline staking

Offline staking lets one wallet own coins and another wallet stake them. The owner keeps
the key that spends the coins; the staking node has a key that can only stake them. It has
been in the code since 3.0 (active from the governance height, 871,500 on the main network)
and had never been tested here. This document describes how it works, how to use it, a
defect in its validation that 4.0 closes, and what it does not do.

## How it works

An offline staking address is made of two ordinary addresses:

```
getofflinestakingaddress "staking address" "spending address"
```

- the **staking address** is an address of the staking node (its key is on the server);
- the **spending address** is an address of the owner (its key stays wherever the owner
  keeps it; it is not needed for staking).

Coins sent to the offline staking address have the script

```
OP_OFFLINE_STAKE OP_IF  <checks the staking key>  OP_ELSE  <checks the spending key>  OP_ENDIF
```

`OP_OFFLINE_STAKE` is true only inside a coinstake (the stake transaction of a block). So
the staking key signs when the coin is staked, and only then; everywhere else the spending
key is needed.

- The staking node's wallet counts the coins as stakeable and not as its own: `getbalance`
  0, `getbalances` → `offline_staking`, `getstakinginfo` → `weight`.
- When a coin is staked, the coin and the reward go back to the same offline staking
  address. **The reward belongs to the owner. The staking node earns nothing**: there is
  no fee for the operator in this scheme.
- The owner spends the coins at any time, staked or not, with an ordinary transaction.
- `scantxoutset "start" ["staking(<staking address>)"]` on any node lists what is delegated to
  a staking key; `"staking(*)"` lists every offline staking output of the chain.

`feature_offline_staking.py` tests all of this.

## The defect, and the rule that closes it

The script lets the staking key sign in any coinstake. Where the coins may go is therefore
a rule of block validation. That rule was: *if the first input (the kernel) of a coinstake
is an offline staking output, every output must pay the script of that input.* The other
inputs were not looked at, and the amounts were not compared. So the holder of a staking
key could

1. **take the delegated coins**: build a coinstake whose kernel is a coin of his own (an
   ordinary one, or an offline staking coin with his own spending key), add the delegated
   coins as further inputs, which his staking key signs, and pay everything to himself;
2. **destroy them**: pay out less than the coinstake takes in.

Both need a block of one's own, so a kernel that wins; a holder of the staking key who puts
a few thousand coins of his own to stake gets one within hours or days. "The staking server
cannot move the coins" did not hold: whoever had the staking key, the operator or someone
who broke into the server, could take everything that was delegated to it.

4.0 (`CheckOfflineStakeScripts()` in `pos.cpp`, used by `ConnectBlock()` and
`CheckProofOfStake()`): when a coinstake spends an offline staking output,

- every input must have that very script,
- every output must pay that script,
- the outputs must not be worth less than the inputs.

The wallet always built its stakes like that (it combines only coins of the kernel's
script), so nothing changes for honest stakers.

**Consensus.** This is a tightening of the rules. It is applied without an activation
height because no block can be affected: the main network has never had an offline staking
output. Checked on 2026-11-13: the byte sequence that begins every such script
(`c6 63 76 a9 14`) does not occur in the 2.1 GB of blocks 1 to 3,031,432, nor in blocks
3,031,433 to 3,034,258, and the chain state at 3,031,432 has no such output. Until a
staking majority runs 4.0 the old nodes would still accept such a theft; with the stake of
the main network on 4.0 a block that contains one is refused and does not stay in the
chain.

Unit tests: `offlinestake_tests` (what the script allows, and each case of the rule).

## How to use it

On the staking node (4.0, running with `-staking=1`):

```
aokchain-cli getnewaddress            # the staking address; give it to the owner
```

The owner, on a computer that need not be online afterwards:

```
aokchain-cli getnewaddress            # the spending address; back up this wallet
aokchain-cli getofflinestakingaddress "<staking address>" "<spending address>"
```

Then send the coins to the offline staking address, from any wallet. After 60 blocks the
staking node stakes them. To end it, the owner sends the coins anywhere else.

## Limits

- **The staking node needs 4.0 for the rule above to protect the coins**, and so does the
  majority of the stake.
- **The staking key is a hot key.** Whoever has it can stake the coins and nothing else,
  under the 4.0 rule. He can also refuse to stake them; the owner then moves the coins.
- **The owner's wallet counts the coins as its own money, and a payment leaves them alone.**
  They are in the balance, but a payment does not take them as inputs on its own (it did
  before 4.0rc9, which ended the delegation of those coins unasked); with only delegated
  coins a payment fails with "Insufficient funds". To spend them, select them: coin control
  in the GUI, or a raw transaction. `-spendofflinestaking=1` gives the old behaviour back.
- **No reward for the operator.** A staking service would need an agreement outside the
  chain.
- A stake gathers small coins of the same offline staking address like any stake
  (`-stakecombinethreshold` of the staking node).
- Tokens cannot be at an offline staking address.
- Not tested: an encrypted staking wallet with the staking-only unlock together with
  offline staking, the GUI for offline staking addresses, reorganizations that orphan
  offline stakes.
