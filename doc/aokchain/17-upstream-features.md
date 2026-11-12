# 17. Features taken over from later Bitcoin Core versions

AokChain Core descends from Bitcoin Core 0.16/0.17. Document 07 lists the security fixes of
later versions; this document lists the features (Bitcoin Core 0.17 to 30) that are useful
here, in the order in which they are taken over, and describes what is in already.

None of them changes a consensus rule or the behaviour of an existing command, with the
exceptions that are named.

## Plan

| Phase | Content | Release |
|---|---|---|
| 1 | Commands and options for operators and exchanges (below) — **done** | 4.0.0 |
| 2 | Wallet structure and tools: offline wallet tool, one database environment per wallet and wallet lifetime handling (unloading the first wallet, wallets in the GUI), Branch-and-Bound coin selection and `-avoidpartialspends`, the transaction index as a background index, `restorewallet`, PSBT for the AokChain transaction format | 4.1 |
| 3 | Network: Tor v3 and addrv2, block-relay-only connections and anchors, limits on the peer time adjustment, removal of UPnP (see 07, C-02 to C-06, U-09) | 4.2 |
| 4 | Base: reproducible builds, LevelDB and libsecp256k1 updates, SHA-NI, fuzzing, `std::thread`, CMake (see 07, T-01 to T-06) | 4.2 |

Not planned: SegWit and Taproot (hard fork, no present use), descriptor wallets and SQLite
(need a design for tokens, offline staking and time locks first), assumeutxo (the snapshot
start of document 15 does this), BIP324, compact block filters, package relay, external
signers.

## Phase 1

### Wallet

| Command | Bitcoin Core | Notes |
|---|---|---|
| `unloadwallet ( "wallet_name" )` | 0.17 | Unloads a wallet that was loaded with `loadwallet`, `createwallet` or a second `-wallet`. Its file is closed and made self contained (as `backupwallet` does), so it can be copied, moved or loaded again. **The first wallet cannot be unloaded**: the staking thread, the GUI and the token balance calls use it. The wallet object is kept until shutdown because another request may hold a pointer to it. `loadwallet` now refuses a wallet that is loaded already |
| `getaddressinfo "address"` | 0.17 | The fields of `validateaddress` without `isvalid` (an invalid address is an error), with `label` and `labels` |
| `getbalances` | 0.19 | `mine`: `trusted`, `untrusted_pending`, `immature`, and here also `staked`, `locked`, `offline_staking`; `watchonly` when the wallet watches addresses |
| `signrawtransactionwithwallet "hex" ( prevtxs sighashtype )` | 0.17 | `signrawtransaction` without the list of keys |
| `signrawtransactionwithkey "hex" ["key",...] ( prevtxs sighashtype )` | 0.17 | `signrawtransaction` with keys only; works without a wallet |

`signrawtransaction` stays: exchanges use it.

### Chain and mempool

| Command | Bitcoin Core | Notes |
|---|---|---|
| `scantxoutset "start" [objects]` | 0.17 | The unspent outputs of addresses, read from the chain state: no wallet, no rescan, no past blocks. Scan objects are `"address"`, `"addr(address)"` and `"raw(hex script)"`, as strings or `{"desc": ...}`; there are no key descriptors. An address also matches the outputs that pay to its public key (stakes do that) and its outputs with token data (`"token": true`, amount 0). The scan runs inside the call (about 4 seconds for the 7.3 million outputs of the main network); `status` and `abort` do not exist |
| `testmempoolaccept ["rawtx"] ( allowhighfees\|maxfeerate )` | 0.17 | Runs every mempool check on a transaction and changes nothing. One transaction per call. `AcceptToMemoryPool()` has a `test_accept` argument for it |
| `sendrawtransaction "hex" ( allowhighfees\|maxfeerate )` | 0.19 | The second argument is still the boolean, or a fee rate in AOK/kB above which the transaction is refused (`0`: no limit). Without it the limit is `-maxtxfee` (10 AOK) for the whole transaction, as before |
| `getblockstats hash_or_height ( stats )` | 0.17 | Transactions, inputs, outputs, sizes, fees (in satoshis) of a block of the active chain, from the block and its undo data. The coinbase and the stake transaction do not count for the fees; `proof_of_stake` and `stake_reward` (outputs minus inputs of the stake) are added. No percentile fee rates, no SegWit fields |
| `getindexinfo ( "name" )` | 0.21 | `txindex`, `tokenindex`, `addressindex`, `spentindex`, `timestampindex` when they are on. They are written with every block, so `synced` is always true |

### Node

| Command or option | Bitcoin Core | Notes |
|---|---|---|
| `getnodeaddresses ( count )` | 0.18 | Known peer addresses, a random selection |
| `getrpcinfo` | 0.18 | The commands that are running now, with their running time |
| `-rpcwhitelist=<user>:<method>,...`, `-rpcwhitelistdefault` | 0.20 | Restricts an RPC user to a list of methods (HTTP 403 otherwise; a batch is refused as a whole). A user without an entry may call everything, unless `-rpcwhitelistdefault=1`, which is the default as soon as one whitelist is given. The cookie user is `__cookie__` |
| `-startupnotify=<cmd>` | 0.21 | Runs when the node has started and answers |
| `-shutdownnotify=<cmd>` | 25 | Runs before the shutdown begins; the node waits for it |
| `-daemonwait` | 22 | Like `-daemon`, but the command returns when the node has started, with a failure status when the start failed. With `Type=forking` systemd then shows the service as active only when it answers |
| `-logratelimit` (default 1) | 30 (PR 32604) | A source location that logs more than 1 MiB within an hour without a debug category is suppressed for the rest of that hour; one line says so, and one line reports the dropped bytes afterwards. `error()` is limited per format string. The tip line of the initial sync and the `-debug=` categories are not limited. Closes U-14 of document 07 (a peer that can provoke a message can no longer fill the disk). The functional tests run with `-logratelimit=0` |

### Example: an RPC user for an exchange front end

```
rpcauth=front:<salt>$<hash>            # share/rpcuser/rpcuser.py makes this line
rpcwhitelist=front:getblockcount,getblock,getblockhash,getrawtransaction,testmempoolaccept,sendrawtransaction,scantxoutset
rpcwhitelistdefault=0                  # the other users stay as they are
```

### Tests

`rpc_upstream.py` (all of the above except the log limit), `wallet_unload.py`,
`feature_staking.py` (block statistics of a proof-of-stake block), unit test
`util_tests/log_rate_limit_test`.

Not tested: `scantxoutset` and `getblockstats` on the main network data, `-daemonwait` under
systemd, the whitelist with cookie authentication only.
