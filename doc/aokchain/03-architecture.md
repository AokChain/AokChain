# 03. Architecture

AokChain Core is a fork in the lineage **Bitcoin Core 0.16~0.17 → Ravencoin → AokChain**.
On top of the Bitcoin skeleton, the following has been added or changed.

- Proof-of-Stake (BlackCoin v3 family kernel, PoS only after block 1440)
- Extended transaction format (`nTime`, `nMessage`)
- blake2b block hash + PoS block signature (`vchBlockSig`)
- Token system (Ravencoin assets renamed to "tokens" + the USERNAME type)
- Offline (cold) staking script
- CLTV timelock output type
- Governance (freeze/unfreeze and changes of fees and costs by a master address)

> The figures in this document are based on the code of the `modernize` branch as of 2026-11. Update them together with any change.

---

## 1. Chain parameters (`src/chainparams.cpp`, `src/chainparamsbase.cpp`)

| Item | mainnet | testnet | regtest |
|---|---|---|---|
| P2P port | 33441 | 44551 | 18444 |
| RPC port | 33440 | 44550 | **44550 (collides with testnet)** |
| Magic | `3b ee e0 02` | `44 4b 52 04` | `43 52 4f 57` ("CROW", a Raven leftover) |
| Base58 pubkey/script/offline/secret | 46 / 108 / 63 / 255 | 66 / 128 / 125 / 143 | 111 / 196 / 115 / 239 |
| xpub / xprv | 0488B21E / 0488ADE4 | 043587CF / 04358394 | same as testnet |
| Last PoW block (`nLastPOWBlock`) | 1440 | 1,440,000 | 1,440,000 (can be lowered with the regtest-only `-lastpowblock`) |
| Token / token P2SH / token IPFS activation height | 159500 / 450000 / 622000 | 10 / 15 / 20 | 10 / 10 / not set |
| `nDisableTimestampCheck` | 695000 | 0 | 0 |
| Governance activation (`nGovernanceHeight`) | 871500 | disabled (INT_MAX) | 10 |
| Last checkpoint | 860000 | none | none |
| Maximum reorg depth | 500 | 500 | 60 |
| DNS seeds | `dns.aok.network` (only one) | none | none |

- Block interval `nTargetSpacing` = 64 seconds, `nTargetTimespan` = 16 minutes. Separate powLimit/posLimit.
- SegWit disabled. CSV, BIP34/65/66 active.
- Reward (near `GetBlockSubsidy` in `validation.cpp`): block 1 = 2,100,000,000 AOK (premine), then 4 AOK/block, halving every 525,960 blocks, 0 after 10 halvings. The PoS reward uses the same function.
- Maturity (`consensus/consensus.h`): `COINBASE_MATURITY` = 10, `COINSTAKE_MATURITY` = 60.
- Versionbits deployments: only TESTDUMMY exists (height-based activation is used).

## 2. Differences from Bitcoin in the consensus layer

### 2.1 Proof-of-Stake (`src/pos.cpp`, `src/pos.h`)
- Port of the BlackCoin v3 kernel, **no coin age**.
- Stake modifier: `Hash(kernel || prev.nStakeModifier)`.
- Kernel: `hash(nStakeModifier, coinPrev.nTime, prevout.hash, prevout.n, nTimeBlock) / nValueIn <= target`.
- Coinstake time = block time. The minimum stake "age" is not a time but 60 confirmations.
- `CheckProofOfStake` (pos.cpp) verifies the input signature and the offline staking rules.
- Kernel/stake verification was originally performed **only at the `ConnectBlock` stage** → "fake stake" risk. Since `e5c3203`/`d4e3bb2`, blocks whose parent is on the active chain are also checked before they are stored (SEC-06, partly fixed; see 04-security-audit.md).
- Block signature: `SignBlock` (`miner.cpp`), `CheckBlockSignature` (`validation.cpp`).
- Validating a PoS block needs a lookup of the previous transaction (`GetTransaction`) → `-txindex` is on by default, pruning is not possible.

### 2.2 Transactions (`src/primitives/transaction.h`)
- `nTime` is serialized after `nVersion`.
- If `nVersion >= 2` (MESSAGE_VERSION), `std::string nMessage` is serialized after `nLockTime`.
- `CURRENT_VERSION = 1`, `MAX_STANDARD_VERSION = 2`.
- ⚠ Serialized test vectors (hex) from Bitcoin/Raven cannot be used as they are.

### 2.3 Blocks (`src/primitives/block.h/.cpp`)
- Header hash = `blake2b(80-byte header)` (`hash.h`, `crypto/blake2b.c`). Not Groestl/X16R.
- `vchBlockSig` added to `CBlock` (serialized after vtx). Also included in compact blocks (`CBlockHeaderAndShortTxIDs`).

### 2.4 Script types (`src/script/standard.h`)
- `TX_NEW_TOKEN`(8), `TX_REISSUE_TOKEN`(9), `TX_TRANSFER_TOKEN`(10)
- `TX_CLTV`(11): `<n> OP_CHECKLOCKTIMEVERIFY OP_DROP` + P2PKH. Queried with `CTxOut::GetLockTime()`.
- `TX_OFFLINE_STAKING`(12): `OP_OFFLINE_STAKE`(0xc6). The address is the OFFLINE prefix + two 20-byte key IDs (staking key, spending key).
- `Solver()`, `ExtractDestinations()` and `IsStandard()` have an additional **`scriptType` output argument** (the inner type of a token script).
- `IsMine()` has an additional `CBlockIndex* bestBlock` argument.
- `std::pair<CKeyID, CKeyID>` (offline staking) has been added to the `CTxDestination` variant → it must be handled when writing a visitor.

### 2.5 Governance (`src/governance/`)
- `CGovernance`, based on LevelDB.
- Trigger: a transaction that spends a script output of the mainnet master address (`kp5eNwMzDfDgHXMG3XZw73c6rTA6WzFJEx`) (after `nGovernanceHeight`).
- Payload: OP_RETURN, marker `'G''A'`. Actions: freeze `'F'`, unfreeze `'U'`, change token cost `'C'`, change token fee script `'f'`.
- Application: inside ConnectBlock in `validation.cpp` (about L2779-2870), rollback about L2104-2175.
- Enforcement: `consensus/tx_verify.cpp` rejects spends of frozen scripts.
- RPC: `freezelist`, `checkfreeze`, `issuanceinfo`.
- ⚠ A single key can freeze arbitrary addresses — the operating policy needs to be documented.

## 3. Token system (`src/tokens/`)

| File | Role |
|---|---|
| `tokens.cpp` (~3200 lines) | Name rules, script parsing, fee checks, creation of wallet transactions |
| `tokens.h` | `CTokens`, `CTokensCache` |
| `tokendb.cpp/.h` | `CTokensDB` (CDBWrapper) |
| `tokentypes.h/.cpp` | `CNewToken`, `CTokenTransfer`, `CReissueToken` etc. |

- Types (`KnownTokenType`): ROOT=0, SUB=1, UNIQUE=2, OWNER=3, REISSUE=6, USERNAME=7, INVALID=8.
- Protected names (`PROTECTED_NAMES`): `AOK`, `AOKCHAIN`, `AOKCOIN`, `AOKHACOIN`, `AOKHACHAIN`.
- Fees: paid to a **single `TokenFeeAddress`** instead of Raven's burn addresses. The amount is `governance->GetCost(type)` (the initial values come from chainparams). Mainnet defaults in chainparams: root 10000, reissue 10000, unique 100, sub 100, username 1 AOK (the governance state on mainnet as of 2026-11-11 is different, see 01-status.md).
- Consensus validation: `Consensus::CheckTxTokens` (`consensus/tx_verify.cpp`).
- RPC (`rpc/tokens.cpp`): `issue`, `issueunique`, `registerusername`, `getusernameaddress`, `listtokenbalancesbyaddress`, `listuniquetokenholders`, `gettokendata`, `listmytokens`, `listmylockedtokens`, `listaddressesbytoken`, `transfer`, `transfermany`, `transfermanyoutputs`, `transferfromaddress`, `reissue`, `listtokens`, `getcacheinfo`.

## 4. Library layering (`src/Makefile.am`)

```
aokchaind = server + common + univalue + util + wallet + zmq + consensus + crypto + leveldb + secp256k1
```

| Library | Main contents |
|---|---|
| `libaokchain_server` | validation, net, net_processing, init, miner, pos, txdb, txmempool, rpc/*, **tokens/***, **governance/***, script/ismine, consensus/tx_verify |
| `libaokchain_common` | base58, chainparams, coins, core_read/write, key, keystore, script/standard, script/sign |
| `libaokchain_consensus` | primitives, script/interpreter, hash, pubkey (→ `libaokchainconsensus.so`) |
| `libaokchain_util` | util, sync, random, fs, clientversion |
| `libaokchain_wallet` | wallet/* |

**Known layering violations** (a lower layer → references server):
- `coins.cpp`, `core_write.cpp`, `script/standard.cpp` (common) → tokens / validation
- `script/script.cpp`, `primitives/transaction.cpp` (consensus) → tokens (member definitions such as `CTransaction::IsNewToken` are in tokens.cpp)
- Result: `aokchain-tx` has to link the same set of libraries as aokchaind **twice** (see the comment in `src/Makefile.am`).
- `CTxOut::GetLockTime()` was moved to `script/ismine.cpp` for the same reason (to resolve an undefined symbol in the consensus .so).
- Long-term task: move token script parsing (pure functions) to common/consensus and leave only the state-dependent logic in server.

## 5. Versions

- Client: `_CLIENT_VERSION_*` in `configure.ac` (currently 4.0.0, release candidate; the deployed network runs 3.0.x).
- Protocol (`src/version.h`): `PROTOCOL_VERSION` 70021 (3.0.x: 70020), `MIN_PEER_PROTO_VERSION` = 60018, `TOKENDATA_VERSION` 70017.

## 6. RPCs specific to AokChain

- Tokens: the list in section 3
- Governance: `freezelist`, `checkfreeze`, `issuanceinfo`
- Staking: `getstakinginfo`, `setgenerate`, `getgenerate`, `getofflinestakingaddress`
- Address/spent index (Raven/Dash family): `getaddressmempool`, `getaddressutxos`, `getaddressdeltas`, `getaddresstxids`, `getaddressbalance`, `getaddresses`, `gettokentransactions`, `getspentinfo`, `getblockdeltas`, `getblockhashes`
- Wallet: `listunspenttoken`, `removeaddress`
