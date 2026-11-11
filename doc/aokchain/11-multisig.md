# Signing and multisig (GUI)

AokChain Core has long had message signing and P2SH multisig, but they required combining RPCs by hand in the console, so ordinary users could not use them. This document describes the "Sign" page, which makes them usable from the wallet GUI.

- **Consensus, validation, P2P and the wallet file format were not changed.** There are no new RPCs either. The GUI merely calls the existing RPCs and wallet functions in order.
- The terms **multisig, public key and signature** are used as they are, and their meaning is explained inside the GUI (the "What is multisig?" window).
- Code: `src/qt/multisigmodel.{h,cpp}` (behaviour), `src/qt/signhubpage.{h,cpp}` (GUI). Functional test: `test/functional/wallet_multisig_flow.py`.

## Pages

| Page | What it does |
|---|---|
| Sign (first page) | 3 task cards (Prove my address, Check a signature, Multisig) and the list **My multisig wallets** (name, rule chip, address, balance, "My signature needed: N"/"Waiting for signatures: N", Send). Copy my public key, Open a signing request, Add a multisig wallet I was invited to |
| Prove my address | Choose one of my addresses and write a text; address, text and signature come out as **one block of text**. No coins move |
| Check a signature | Paste that block and it is split into its three parts and checked immediately. The parts can also be entered separately. The result is a large "Valid / Not valid" with the reason |
| Make a multisig wallet | 3 steps: Set the rule → Gather the participants → Check and finish |
| Add a multisig wallet I was invited to | Paste the multisig information sent by the creator, or open it from a file, to add the same wallet |
| Send from (wallet name) | Recipient address, amount (All), memo, fee → "Make the send request". On the right the requests in progress and the wallet information |
| Send request | Progress strip (Request made → Signed k/M → Broadcast), the actual contents of the transaction, the next step, signatures, export (file, code, QR), Load signatures I received, Sign, Broadcast, Cancel |

## Order of use

1. **Collect the public keys.** Each participant uses Sign → "Copy my public key" in their own wallet to copy `aokms-key1:…` and sends it to the person who makes the wallet.
2. **Make it.** The maker enters the name, the rule (M-of-N) and the names and public keys of the participants and finishes. The multisig address is shown.
3. **Share the information.** The maker sends `aokms-wallet1:…` to the other participants with "Save the multisig information file" or "Copy the information code". The recipients put it into "Add a multisig wallet I was invited to".
4. **Deposit.** Send to the multisig address like to an ordinary address.
5. **Send request.** One participant makes a request (their own signature is added right away). They send the request to the other participants as a file, code or QR (`aokms-req1:…`).
6. **Sign.** The recipient opens it with "Open a signing request", checks the contents and clicks "Sign". If signatures are still missing, they pass it on to the next person.
7. **Broadcast.** Once M signatures have been collected, anyone who has the request clicks "Broadcast". The signed request can also be sent back to the maker and merged with "Load signatures I received".

There is **no feature that delivers requests between the participants automatically.** They send them themselves by messenger, e-mail and so on.

## What the GUI calls

| GUI action | Internally |
|---|---|
| Copy my public key | One new key from the key pool (`GetKeyFromPool`), recorded in the address book as "Multisig key". The same key is handed out again until it has been used in a multisig wallet |
| Computing the multisig address | The P2SH address of `m <keys> n OP_CHECKMULTISIG` after **sorting** the public keys. The same result as `createmultisig m [sorted keys]` |
| Making or adding a multisig wallet | `importaddress <redeemScript> <name> <rescan> true` — the wallet learns the redeem script and watches the address as watch-only. A newly made address is added without a rescan; when adding by invitation, a rescan can be chosen (on by default) |
| Balance | Sum of `listunspent 1 9999999 [address]` (1 or more confirmations) |
| Making a send request | Coin selection with `listunspent` → `createrawtransaction [inputs] {recipient address: amount, multisig address: change}` → `signrawtransaction <hex> [prevtxs]` |
| Sign | `signrawtransaction <hex> [{"txid","vout","scriptPubKey","redeemScript","amount"}]` (uses the wallet keys; a locked wallet is unlocked first) |
| Load signatures I received | `combinerawtransaction [existing, received]` |
| Broadcast | `sendrawtransaction <hex>` |
| Prove my address / Check | The same computation as `signmessage` / `verifymessage` (the signature format is the same, so they are interoperable) |

**Sorting of public keys.** The redeem script depends on the order of the keys, and a different order gives a different address. The public keys are sorted in byte order (order of the hexadecimal strings, the same way as BIP67), so every participant gets the same address no matter who enters the keys in which order. A request with an unsorted script is refused as "a multisig address that was not made with this wallet program".

**Coin selection and fee.** Among the confirmed coins of that address that are not used by another request in progress, coins are chosen in descending order of amount (ties by txid, then output index). The change returns to the same multisig address (if it is dust-sized it is added to the fee). "All" uses every coin and has no change. The fee is set by computing the size of the transaction with all M signatures in it and applying the wallet's fee rate (`GetMinimumFee`). "Faster" is twice that. The size formula is the same as `signed_size()` in the functional test, and the test compares it with the real size (it deviates only towards being a few bytes per input larger than the real size).

## The three kinds of text that are exchanged

All of them are a single line consisting of `prefix:` followed by JSON written in base64. A file (`.aokms`) contains the same text. Line breaks and whitespace are ignored when pasting.

| Prefix | Contents |
|---|---|
| `aokms-key1:` | A compressed public key, 66 characters (hexadecimal). Not JSON. A hexadecimal public key without the prefix is accepted too |
| `aokms-wallet1:` | `{"v":1,"name","m","keys":[{"name","pubkey"}…],"address","redeemScript"}` — no private keys |
| `aokms-req1:` | `{"v":1,"wallet","address","redeemScript","memo","to","amount","tx","prevouts":[{"txid","vout","scriptPubKey","amount"}…],"created"}` — `tx` is the raw transaction (hexadecimal) with the signatures in it |

The block of signed text:

```
-----BEGIN AOKCHAIN SIGNED MESSAGE-----
<text>
-----BEGIN SIGNATURE-----
<address>
<signature (base64, 88 characters)>
-----END AOKCHAIN SIGNED MESSAGE-----
```

It is read even if a messenger joins the lines (address and signature are separated by their length). If the text has several lines, the line breaks must be preserved for the signature to match.

## Storage

The list of multisig wallets and the requests in progress are stored in the **existing record type `destdata` of the wallet file (wallet.dat)**, as entries of the multisig address (`CWallet::AddDestData`). There is no new record type and no separate file.

| destdata key | Attached to | Value |
|---|---|---|
| `msigwallet` | The multisig address | Wallet information JSON (name, m, the named public keys, address, redeem script, last balance read) |
| `msigreq_<first 16 characters of the id>` | The multisig address | Request JSON (the same as `aokms-req1` above, plus `txid` if this wallet broadcast it) |
| `msigkey` | My ordinary address | `unused` / `used` — the key handed out by "Copy my public key" |

- The multisig address itself goes into the address book as a **receiving address**, with the wallet name as its label. It appears in the transaction history under that label.
- Which key is mine is not stored; each time, the wallet is checked for whether it has that key.
- A wallet backup (wallet.dat) includes the contents above. The GUI nevertheless advises keeping the multisig information file separately (it can be added again from another wallet file).
- The coins of a multisig address are counted in the total wallet balance as **watch-only** balance (a watch-only row appears on Overview). They are not used for staking.

## What is checked when a received request is opened

The description written in the request is not trusted. The amount, recipient address, change and fee shown on screen are **values decoded from the raw transaction**, and the input amounts are read from the **UTXO set of this node (including the mempool)**.

| Check | If it fails |
|---|---|
| Is the redeem script that of a multisig wallet this wallet knows | Cannot sign. The participants (public keys) are shown and the wallet has to be added first |
| Are all inputs unspent coins known to this node | Cannot sign ("already broadcast or used by another transaction, or not synced yet") |
| Are all inputs coins of that multisig address | Cannot sign |
| Are all outputs ordinary payments to addresses (no data or token outputs) | Cannot sign |
| Do all signatures it contains match this transaction and the public keys of this wallet (only SIGHASH_ALL is accepted) | Cannot sign ("the transaction may have been changed after someone signed it") |
| Do the recipient address and amount written in the request equal those of the actual transaction | Warning (the real values are shown on screen), and a warning in the signing confirmation dialog too |
| Is there exactly one destination other than the multisig address | Warning ("sends to N addresses") |
| Is the fee more than 10 times the expected one and larger than 0.1 AOK | Warning |

- **Who has signed** is determined not from the description in the request but by verifying the signatures in the scriptSig of the transaction against each public key.
- Signing is never automatic. Clicking "Sign" shows once more what is being signed and asks for confirmation. Only the wallet of the person who makes a request adds its own signature right away when making it.
- Broadcasting goes through a confirmation dialog as well.

## Limitations and what was not done

- **AOK only.** **Sending tokens** from a multisig address **was not implemented** (the GUI says it is not available yet). Token balances are not shown either. Outputs that carry tokens are not included in coin selection.
- **P2SH multisig only**, 2~15 participants (the 520-byte limit of the redeem script), compressed public keys only.
- **No change of rule or participants.** To change them, make a new wallet and move the coins.
- A **QR** code is offered only when the request code is 2,000 characters or less (a 2-of-3 request with 1 input is about 1,650~1,850 characters). Above that the button is disabled and the reason is in the tooltip. The QR code was not actually scanned.
- **Cancelling a request** removes it from the list of my wallet. A request that has already been signed and passed on can still be completed and broadcast by someone else (the GUI says so).
- A request can still be used long after it was made, but if the same coins are broadcast through another request in the meantime it becomes a "request that can no longer be used". The coins used by a request in progress are left out of the coin selection of a new request (known only within the same wallet).
- Right after broadcasting, the change is missing from the multisig balance until it is confirmed (the balance counts only 1 or more confirmations).
- There is no hardware wallet support, no PSBT (BIP174) and no SegWit multisig.

## What was verified

- **Functional test** `wallet_multisig_flow.py` (3 regtest nodes): the same 2-of-3 address even when the keys are entered in a different order, all three wallets recognizing the deposit as watch-only, a spend whose change returns to the multisig address, A signs (incomplete) → B signs (complete), `combinerawtransaction` of signatures made separately, broadcast, confirmation and balance, **rejection with 1 signature**, **A's signature becoming invalid when an output is changed**, sending everything, the size computation being at least the real size and close to it.
- **GUI (regtest, 3 wallets run in turn, ko_KR)**: Copy my public key → make a 2-of-3 (including rejection of a duplicate public key) → still in the list after restarting → add to B and C with the information code (balance shown after the rescan) → make a request for 12,000 AOK in A (own signature) → export as code and QR → open in B → after restarting, "My signature needed: 1" → Sign → send back to A as a code and merge → broadcast from A → check through RPC the transaction included in a block (12,000 to the recipient address, change to the multisig address, 2 signatures). A request with a changed output was blocked from signing in C with a red warning. Sign a text → copy the block → valid in another wallet / not valid when the text is edited / a block with joined lines is valid too.
- **Not tried on screen**: an encrypted wallet (unlock dialog), a multisig wallet added as watch-only only, the dialog that adds the wallet when a request of an unknown multisig wallet is opened, saving to and opening from a file (only pasting codes was used), "All", the "Faster" fee, renaming and removing, more than 3 participants, the display of a request with several inputs. Of these, sending everything and several inputs are covered by the functional test through RPC.
