# AokChain Core 4.0.0 (candidate) changelog — compared with 3.0.5

- **Covers:** 4.0.0 release candidate (`modernize` branch, Windows test build `v4.0.0.0-0698aa2`)
- **Baseline:** 3.0.5 (`5d1fd3d`, 2022-09-24)
- **Size:** 96 commits, 277 files changed (as of `08bd166`)
- **Status:** pre-release. Not yet observed on testnet and not code-signed.

## Compatibility

- **No hard fork.** There is no activation height. The whole existing mainnet chain (about 3.03 million blocks) was revalidated from scratch with 4.0.0 and no block was rejected.
- **Connects to existing peers.** The network magic is unchanged; the protocol version goes from 70020 to **70021** (3.0.x accepts peers with a higher version). A mechanism that drops 3.0.x peers from a set height on is included but **disabled on mainnet and testnet** (`13-old-version-cutoff.md`). It was run against 3.0.2 and 3.0.5 peers and follows the tip.
- **Same wallet file format.** Release builds use BDB 4.8, so a 3.0.x `wallet.dat` opens as before.
- **Some rules are stricter.** Items 1 to 3 under "Security" reject blocks that 3.0.x wrongly accepts. Honest blocks are not affected, but an attack block would split 3.0.x from 4.0.0, so major stakers and exchanges should upgrade together.

## Security

1. **Block signature checks restored.** With the default multi-core setting, 3.0.x did not look at the result of signature verification for transactions in a block. A block containing a transaction with a bad signature was accepted.
2. **Token amount overflow closed.** Outputs whose sum wrapped around the integer range could create tokens out of nothing.
3. **Forged governance commands closed.** Because of item 1, address freezes, token cost changes and fee address changes could be applied without the master key. The signature is now verified before a command is applied.
4. **Remote crash fixed.** Receiving BLOCKTXN twice for the same block shut the node down.
5. **Crash while serving block requests fixed.** A peer's block request arriving right after another thread accepted a block shut the node down. This could happen occasionally on staking nodes.
6. **Fake-stake defence.** The kernel and stake input of a proof-of-stake block are checked before the block is written to disk.
7. **Mutated blocks rejected early.** A block with a wrong merkle root or block signature can no longer disturb the download state of other peers.
8. **P2P abuse limits.** Rate limit for addr messages, one GETHEADERS per INV, overflow-safe peer time offset.

## Staking

- **100% CPU fixed.** With an unlocked wallet that had nothing to stake (no balance, or everything immature right after a stake) the staking thread never slept, used a full core and slowed the whole node down. Reproduced on mainnet: 100% before, 0% after.
- **No repeated searches.** The kernel search is not repeated for the same block and the same 16 second slot. The full block is assembled only after a kernel was found (as in Qtum).
- **Locking fixed.** The kernel search read the UTXO cache without holding the locks.
- **`getstakinginfo` fixed.** `staking` and `expectedtime` were always 0, even while staking.
- **Orphaned stakes.** Stakes that lost against another block are left out of balance scans and are cleaned up when the wallet is opened (as in Qtum).

## Wallet performance

A wallet that had been used for a long time became very slow as transactions accumulated. Balance queries, sending and staking walked every past transaction of the wallet on every call. They now look only at transactions that still have spendable outputs. The results (balances, list of unspent outputs) are the same as before.

| Item (wallet with 30,000 transactions, regtest) | 3.0.x code | 4.0.0 |
|---|---|---|
| `getbalance` | 67 ms | 2 ms |
| `sendtoaddress` | 152 ms | 18 ms |
| `listaddressgroupings` | did not finish in 11 minutes | 1.4 s |
| Staking thread CPU | 26.5% of a core | 0.14% |

## New features

- **Private keys from a mnemonic.** Derives keys from the BIP39 recovery words of another wallet, such as the mobile wallet.
  - `mnemonicfindaddress`: finds the derivation path and private key from one address of that wallet.
  - `mnemonictokeys`: lists addresses and private keys below a path.
  - The checksum is validated against the English and Korean word lists. Nothing is stored.
- **Sign and multisig pages (GUI).** The "Sign" page of the wallet proves an address and checks a signature that was received, makes a multisig wallet or adds one you were invited to, and sends AOK from a multisig address with a signing request that is passed between the participants. It is built on the existing P2SH multisig and raw transaction RPCs; consensus and the wallet file format are unchanged. Tokens cannot be sent from multisig yet. See `11-multisig.md`.
- **Fast first synchronization.** (1) assumevalid moved to block 3,030,000. (2) Block bootstrap: block archives are fetched over HTTP(S) or read from a folder and imported, and every block is validated as usual. The GUI wallet has a built-in address for mainnet. `-bootstrapurl` (several mirrors), `-bootstrapdir`, `-bootstrap=0`, RPCs `getbootstrapinfo`/`skipbootstrap`, tool `contrib/aokchain/make_bootstrap.py`. See `12-bootstrap.md`.
- **Language choice.** A "Language" row at the top of the settings page (each language in its own language) with "Restart now"; the same selector in the first-run dialog (data directory).
- **`purgeorphanstakes`.** Deletes orphaned stakes that pile up in the wallet file (run manually).
- **`-checkwalletlive`.** Test option that verifies the wallet index against a full scan.
- **`-lastpowblock`.** Lets regtest reach the proof-of-stake phase (regtest only).

## Bug fixes

- **`-reindex`:** stopped at a block stored before its parent (at height 3,440 with mainnet data).
- **`-reindex-chainstate`:** did not wipe the token and governance databases and marked valid blocks invalid.
- **`removeprunedfunds`:** left traces of the removed transaction that could later shut the node down.
- **GUI:** the balance on the Send and token pages was never refreshed; locked balance display.
- **`getwalletinfo`:** printed `hdmasterkeyid` when there was no HD seed.
- **Address index:** use of an uninitialised value.
- **`aokchain-tx`, `libaokchainconsensus`:** build and link errors.

## Build and dependencies

- **Compiler:** C++17. Builds on current systems (Ubuntu 24.04, GCC 13). 3.0.5 did not build there.
- **Dependencies:** Boost 1.64 → 1.83, OpenSSL 1.0.1k → 3.0.15, libevent 2.1.8 → 2.1.12, ZeroMQ 4.2.2 → 4.3.5, miniupnpc 2.0 → 2.2.7. BDB stays at 4.8.30.
- **Windows build:** Qt 5.7.1 → 5.15.10 (static, cross-compiled). Merged into `modernize`.

## Tests

- **Unit tests:** 73 of 78 pass. 3.0.5 did not compile them. The remaining 5 need new bloom filter test data.
- **Functional tests:** a baseline of 38 passes (including the multisig flow `wallet_multisig_flow.py`, the bootstrap `feature_bootstrap.py` and the old peer cutoff `p2p_old_peer_cutoff.py`). None ran on 3.0.5. The 47 that still fail expect Bitcoin or Ravencoin values and have not been ported.
- **Mainnet:** full chain revalidation, history scan, live tip following, two real payments confirmed.

## Known limitations

- **Peer discovery:** the DNS seed does not answer and most fixed seeds are dead, so a fresh install may need peers added with `addnode`.
- **Mainnet staking:** staking is running on mainnet but has not produced a block yet, so acceptance of a block made by 4.0.0 is not confirmed.
- **Windows:** tested under wine only. Long-term operation on real Windows is not confirmed.
- **Mnemonics:** only English and Korean are validated. The derivation scheme of the AOK mobile wallet is not published.
- **Not code-signed.** Windows SmartScreen shows a warning.
