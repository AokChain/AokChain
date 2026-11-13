# AokChain Core 4.0.0 release notes (candidate)

> Status: **release candidate**. After deployment and observation on testnet are finished, `_CLIENT_VERSION_IS_RELEASE` in `configure.ac` is set to true and the tag is created.

## Summary
- This is a security upgrade. There is **no hard fork and no activation height**, and it was confirmed that the whole existing mainnet chain (up to height 3,030,487) is accepted as it is.
- All node operators, exchanges and stakers are **strongly advised to upgrade quickly**. 2 block validation defects and 1 remote crash were fixed.

## Changes you must know about

### 1. Block script (signature) verification restored — SEC-01
- **Previous problem:** on multi-core machines (default setting `-par=0`), 3.0.x did not check the result of signature verification for transactions inside a block (since the 2020 "Tokens hardfork" commit). A block containing a transaction with a wrong signature was accepted as well, and the verdict could differ from that of `-par=1` nodes.
- **4.0.0:** signatures are verified in every configuration.
- **Impact check:** the whole of mainnet was validated with `-par=1 -assumevalid=0` (the setting that verifies every signature directly), and the historical main chain contained no violating block. This fix therefore does not change the existing chain.

### 2. Token amount overflow blocked — SEC-02/04/14
- **Previous problem:** 3.0.x did not check the range when computing the sums of token inputs and outputs. With outputs constructed like `INT64_MAX`, `INT64_MAX`, `X+2`, the sum wrapped around and equalled the input `X`, and tokens could be created out of nothing (the same kind as Ravencoin PR #1287). This was confirmed with a reproduction test.
- **4.0.0:** transactions whose amount exceeds `MAX_MONEY_TOKENS` or whose sum overflows are rejected.
- **Impact check:** a scan of the whole of mainnet found no transaction near this limit.
- In addition, the check that a token output must not carry an AOK amount now runs on every path (previously it was skipped on some paths), and the token unit check was changed to integer arithmetic (previously a division by zero was possible).

### 3. Governance forgery blocked — SEC-05
- **Previous problem:** because of the defect in item 1, a 3.0.x multi-core node also accepted a transaction that spends an output of the master address with a bogus signature. The governance commands it carried (freezing addresses, changing token costs, changing the fee address) could be **applied without the master key**.
- **4.0.0:** the signature of a governance transaction is verified directly before its commands are applied.
- **To be checked by operators:** compare the output of `freezelist` and `issuanceinfo` with the intended state.

### 4. Remote crash fixed — SEC-03 (CVE-2024-35202 type)
The node terminated with an assert when it received a BLOCKTXN message for the same block twice; this was fixed.

## Note on the order of deployment
Items 1 and 2 are rule tightenings that take effect immediately, without an activation height. If a block that is valid only on 3.0.x is created, 3.0.x nodes and 4.0.0 nodes can split, and the 4.0.0 chain wins only if a majority of the staking weight is on 4.0.0. **Publish only after agreeing a schedule in which the major stakers and exchanges upgrade first (at the same time if possible).**

## Wallet performance
- **Previous problem:** balance queries, sending and the staking loop walked every transaction of the wallet (including those already spent completely) on every call. The staking thread ran this scan 4 times a second while holding the node-wide locks, so the whole node got slower as the wallet history grew.
- **4.0.0:** fully spent transactions are skipped. The cost follows the number of unspent outputs, not the wallet history. The results (balances, list of unspent outputs, staking weight) are the same as before.
- **Staking thread:** fixed the problem that it spun without resting and used up one core when there were no coins to stake (no balance, everything immature). The kernel search is not repeated for the same tip and the same 16-second slot. Stakes that lost the competition (orphans) are no longer scanned.
- **`listaddressgroupings`:** fixed the problem that it took minutes and stalled the node in wallets with many transactions (the result is the same).
- **Measurements (regtest, 30,000 transactions):** `getbalance` 67 ms → 2 ms, `sendtoaddress` 152 ms → 18 ms, `listaddressgroupings` more than 11 minutes → 1.4 seconds, staking thread CPU 26.5% → 0.14%.
- **Clean-up of orphaned stakes at startup:** when the wallet is opened, its own coinstakes that are not on the active chain are marked as abandoned (the same behaviour as Qtum). The balance is not affected.
- **New RPC `purgeorphanstakes ( minage )`:** deletes orphaned coinstakes that have piled up in the wallet (default: those received more than a day ago). The balance is not affected. It does not run automatically.
- The wallet file format does not change. For details see `08-wallet-performance.md`.

## Extracting private keys from a mnemonic (new RPCs)
- **`mnemonictokeys`:** lists addresses and private keys from the BIP39 mnemonic of another wallet (mobile etc.) and a path.
- **`mnemonicfindaddress`:** when the path is unknown, finds the path and the private key from one address of that wallet.
- They work independently of the wallet and the network and store nothing. The checksum is validated against the English and Korean word lists. For detailed usage see `09-mnemonic.md`.

## Sign and multisig pages (GUI)
- The **Sign** page of the wallet is now organized around tasks: Prove my address, Check a signature, Multisig.
- A **multisig wallet** is made in the GUI (collect public keys → M-of-N address), and the other participants add the same wallet with the multisig information.
- To send from a multisig address, a **signing request** is made and exchanged as a file, code or QR, and it is broadcast once the signatures have been collected. A received request is shown by decoding the actual contents of the transaction, and if something does not match, signing is blocked or a warning is shown.
- It is built on existing core features (P2SH multisig, watch-only addresses, raw transaction RPCs). There are no changes to consensus, P2P or the wallet file format. Only AOK can be sent; tokens not yet. For details see `11-multisig.md`.

## Fast first synchronization
- **assumevalid update:** mainnet `defaultAssumeValid` was moved to block 3,030,000 (`27a0660b…bdf1`), and `nMinimumChainWork` and `chainTxData` were set for that block. The values were read from a node that finished full script validation. The checkpoints and the consensus rules are unchanged.
- **Block bootstrap:** instead of fetching blocks one by one from peers, a freshly installed wallet fetches and imports block archives (about 1.4 GB, 15 files). Every block is validated as usual, so the server does not have to be trusted, and on failure it falls back to peer synchronization. The GUI wallet uses the default mainnet address (GitHub release assets); `aokchaind` uses `-bootstrapurl=http://…` or a downloaded folder with `-bootstrapdir`. Turn it off with `-bootstrap=0`. For details and measurements see `12-bootstrap.md`.

## Old peer cutoff (disabled)
- The protocol version is now 70021. A mechanism that stops accepting peers below 70021 (3.0.x) from a set block height on is included, but it is **disabled on mainnet and testnet.** The height will be set once the deployment schedule has been decided. It is not a consensus rule. See `13-old-version-cutoff.md`.

## P2P / DoS hardening (no fork)
- **Fake stake defence:** the kernel, the stake input and the maturity are checked **before** a PoS block is stored on disk. On mainnet it was observed that PoS blocks with a wrong kernel hash are in fact being broadcast continuously.
- **Mutated blocks blocked:** a block whose merkle root or block signature does not match is rejected right away without touching the "in flight" state (CVE-2024-52921 type). `vchBlockSig` is not part of the block hash, so this mutation used to cost nothing.
- **addr message rate limit:** 0.1 per second per peer, up to a maximum of 1000, are processed (upstream PR #22387).
- **INV handling:** only 1 GETHEADERS is sent per INV message (CVE-2024-52915).
- **Peer time offset:** overflow protection (CVE-2024-52912).
- **miniupnpc:** made compatible with API 18 (2.2.8 and later).

## Bug fixes
- **`-reindex-chainstate`:** now rebuilds the token database and the governance database as well. Previously it stopped at the first token issuance block (mainnet 179,185) and marked a valid block invalid.
- **`-reindex`:** fixed the problem that blocks stored before their parent could not be indexed and the reindex stopped midway (with mainnet data it stopped at 3,440).
- **Crash while handling block requests (SEC-23):** fixed the problem that the node terminated when a peer's block request arrived right after another thread accepted a block. It could happen occasionally on staking nodes.
- **Locking of the staking thread (SEC-24):** fixed the kernel search reading the UTXO cache without locks.
- **`removeprunedfunds` (SEC-25):** fixed the problem that traces of the deleted transaction remained in the internal indexes of the wallet, after which `listtransactions` could read freed memory or the node could terminate with an assert.
- **`getstakinginfo`:** fixed `staking` and `expectedtime` always being 0, even while staking.
- **`aokchain-tx`:** fixed building and linking.
- **`libaokchainconsensus.so`:** removed the undefined symbol (`IsTimeLock`).
- **Address index:** fixed the uninitialized `addressType`.
- **`getwalletinfo`:** fixed `hdmasterkeyid` being printed even when there is no HD seed.

## Build / dependencies
- **Build environment:** C++17 is required. Builds with gcc 13, Boost 1.83 and OpenSSL 3.
- **depends packages:**

  | Package | Before | 4.0.0 |
  |---|---|---|
  | Boost | 1.64 | 1.83 |
  | OpenSSL | 1.0.1k | 3.0.15 |
  | libevent | 2.1.8 | 2.1.12 |
  | ZeroMQ | 4.2.2 | 4.3.5 |
  | miniupnpc | 2.0 | 2.2.7 |
  | BDB | 4.8.30 | 4.8.30 (kept) |

- **Wallet:** the release binaries are built with depends (BDB 4.8), so they are compatible with existing `wallet.dat` files. Wallet files of a binary built with the system BDB 5.3 (`--with-incompatible-bdb`) may not open with 4.8.

## Planned soft fork (not activated in this release)
- **S-05:** the rule that the time of a PoS block must be a multiple of 16 seconds. Stakers already produce blocks according to this rule, and all 3.03 million historical PoS blocks satisfy it. The code is included, but the activation height on mainnet and testnet is undecided (INT_MAX). It will be activated in a separate release after the height has been agreed with exchanges and operators.

## Tests
- **Unit tests:** 57 of 70 suites pass (`contrib/aokchain/unit-tests.sh`). "73 of 78", written here before, was the count of a run that stopped half way. The 13 suites that fail use Bitcoin or Ravencoin test data.
- **Functional tests:** a baseline of 47 passes (`doc/aokchain/functional-baseline.txt`). In the original all of them failed at the import stage.
- **New regression tests:**
  - `wallet_multisig_flow.py`: the multisig flow used by the GUI (2-of-3 address from sorted public keys, partial signing, combining, broadcasting) and the cases that are rejected
  - `feature_bootstrap.py`: synchronization from block archives, falling over to a mirror, importing a folder, damaged files, rejected blocks, restart midway, skipping
  - `p2p_old_peer_cutoff.py`: 70020 peers are refused and disconnected from the set height on, 70021 peers are not affected
  - `feature_block_script_checks.py`: SEC-01
  - `feature_reindex_tokens.py`: reindex bugs
  - `feature_governance_forged.py`: SEC-05
  - `token_tx_overflow_test`: SEC-02
  - `pos_tests`: S-05
  - `wallet_spent_cache.py`: wallet performance cache
  - `bip39_tests`, `rpc_mnemonic.py`: mnemonic RPCs (official BIP39 vectors, comparison with an independent implementation)
  - `feature_staking.py`: regtest PoS staking, orphaned stakes, `purgeorphanstakes` (uses the regtest-only option `-lastpowblock`)
  - All functional tests run with `-checkwalletlive` (exhaustive verification of the wallet index) turned on
- **Mainnet verification:**
  - Full sync (`-par=1 -assumevalid=0`)
  - Full reindex (`-par=8`, 4.0.0 binary)
  - History scan (`contrib/aokchain/history_scan.py`)

## Added in rc3 to rc7

See "Update of 2026-11-13" in `CHANGELOG-4.0.0.md`: coins that lost their time in a reorganization, the size of staking coins, the offline staking rule (SEC-26), `aokchain-wallet`, new commands and options, SEC-08, 13, 15 and 17, and why postponing the halving would be a hard fork.
