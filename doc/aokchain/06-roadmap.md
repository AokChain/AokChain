# 06. Roadmap and change procedure

## 0. Principles

1. **Consensus rules last, and with the greatest care.** Finish the non-consensus improvements such as build, tests, P2P and RPC first to create a safety net, and only then start on consensus changes.
2. **Every change comes with tests.** A bug fix gets a reproduction test; a new feature gets unit and functional tests.
3. **Small commits, separated by kind.** Use the prefixes `build:`, `net:`, `wallet:`, `consensus:`, `test:`, `qa:`, `doc:`. Never mix a consensus change commit with other changes.
4. **Preserve the original history.** The original repository is `origin` (GitHub AokChain/AokChain). When a new repository is created, rename the original to `upstream`.

## 1. Consensus change procedure (mandatory)

A consensus change is any change that alters the **criteria by which a node accepts or rejects** blocks and transactions. This covers ConnectBlock/CheckBlock in `validation.cpp`, `consensus/*`, `pos.cpp`, the IsValid and Check* functions in `tokens/*`, `script/interpreter.cpp`, governance and the chainparams values.

1. **Classification**
   - Soft fork (rule tightening): makes part of what used to be valid invalid. Old nodes still follow the new chain.
   - Hard fork (rule relaxation or change): old nodes split off. All nodes and exchanges have to upgrade.
2. **History scan:** on a node that validates the whole of mainnet, check that applying the new rule to all blocks so far does not make an existing block invalid. If any block becomes invalid, an activation height is mandatory.
3. **Activation height:** add it to chainparams in the form `consensus.nXxxHeight`. For mainnet choose a height at least several weeks ahead, for testnet a low one, and for regtest one that is easy to test.
4. **Tests:** verify the behaviour both before and after activation with unit tests and functional tests.
5. **Deploy on testnet → observe → mainnet release:** state the activation height and the upgrade deadline in the release notes.
6. Review whether `PROTOCOL_VERSION` and `MIN_PEER_PROTO_VERSION` need to be adjusted.

## 2. Phased plan

### Phase 1 — Build modernization and test recovery ✅ (2026-11-09, mostly done)
- [x] Build with C++17, gcc 13, Boost 1.83, OpenSSL 3
- [x] Fix building and linking of `aokchain-tx`
- [x] Restore compilation of the unit tests (73/78 pass)
- [x] Restore the functional test framework (baseline of 38 passes, including regtest PoS staking)
- [x] Fix the undefined symbols of `libaokchainconsensus.so`
- [x] Fix SEC-03 (remote crash)
- [ ] Regenerate the `bloom_tests` vectors
- [x] Raise the client version (3.0.5 → 4.0.0, release candidate) and write release notes (`release-notes-4.0.0.md`)

### Phase 2 — Dependencies and build system (non-consensus)
- [x] Update depends (except the GUI): OpenSSL 3.0.15, Boost 1.83, libevent 2.1.12, zeromq 4.3.5, miniupnpc 2.2.7 (`1a11bda`). The tests pass with binaries built from depends
- [ ] GUI packages in depends: Qt 5.15, latest zlib/expat/freetype (Qt 5.15.10 and qrencode 4.1.1 are done for the Windows host, `f8cdd48`; the Qt depends for Linux remains)
- [ ] BDB decision: keep 4.8 (wallet compatibility) or migrate to SQLite wallets (see upstream 0.21+, a large task)
- [ ] Remove the OpenSSL dependency of `random.cpp` (SEC-07)
- [ ] 0 compiler warnings → `--enable-werror`
- [ ] Set up CI (build, unit tests, functional test baseline): GitHub Actions or a cron/systemd timer on the build server
- [ ] Backport the sanitizer build options

### Phase 3 — Test coverage (non-consensus)
- [x] Adapt the functional test framework to AokChain: add `nTime`/`nMessage` to the Python-side `CTransaction` and `vchBlockSig` to `CBlock` (`128cf0f`)
- [ ] Economic parameter helper (class A), clean-up of the token RPC names (class C)
- [ ] Write PoS functional tests: staking, offline staking, coinstake reward cap
- [ ] Write token functional tests: issue, reissue, transfer, unique, username, fee address
- [ ] Write governance functional tests: freeze, unfreeze, cost change, rollback (reorg)
- [x] Security regression tests: SEC-01 (`feature_block_script_checks.py`), SEC-02 (`token_tx_overflow_test`), SEC-19 (`feature_reindex_tokens.py`)
- [ ] Security regression tests: SEC-03 (resent BLOCKTXN), mutated blocks, fake stake

### Phase 4 — P2P and operational security (non-consensus)
- [x] SEC-06 fake stake defence, phase 1: blocks whose parent is on the active chain get a stake check before they are stored (`e5c3203`, `d4e3bb2`)
- [ ] SEC-06 phase 2: block budget for deep forks, duplicate stake limit, per-peer header budget
- [x] SEC-11 addr rate limit (`a641851`)
- [ ] SEC-13 a separate staking-only flag per wallet
- [ ] SEC-17 update checkpoints, assumevalid, minimumChainWork; diversify the seeds (assumevalid and minimumChainWork done in `13222ef`)
- [ ] SEC-10 document the operating policy for the master key

### Phase 5 — Consensus security fixes (the procedure in section 1 is mandatory)
- [x] Set up a node that validates the whole of mainnet — `<verify-dir>/` (see document 02)
- [x] SEC-01 restore block script verification — no violating block in the history, so fixed without a fork (`0b23d4e`)
- [x] SEC-02/04/14 token amount range checks — 0 violations in the history, fixed without an activation height (`a016189`)
- [x] Code prepared for the stake timestamp mask (S-05) — `nStakeTimestampMaskHeight`, undecided (INT_MAX) on mainnet (`d5ccfef`)
- [ ] A separate release after the S-05 activation height has been settled (in consultation with operators and exchanges)
- [x] SEC-05 block governance forgery: signature verification before application (`10a3fd7`)
- [ ] Rest of SEC-05: governance changes remain when a block fails for a reason other than the signature and when `fJustCheck` is set (collect the applications and do them at the end, while keeping the meaning of the order within a block)
- [ ] SEC-08/09/14/15 (decide each after a history scan)
- [ ] Consider bundling the items above into a single "security hard fork/soft fork release" managed with one activation height

### Phase 6 — Feature upgrades (after the requirements have been settled)
Add to this section once the feature requirements are decided. Candidates for review:
- Backports of upstream Bitcoin Core features (descriptor wallets, compact block improvements, network layer improvements)
- Clean-up of the token layer: move the pure parsing functions down to common/consensus and resolve the layering violations (document 03, section 4)
- Multisig or timelocked governance
- GUI: it builds with system Qt 5.15 (confirmed 2026-11-11). The Windows cross build with static Qt 5.15.10 from depends is done. What remains is the Qt depends for Linux, testing the GUI on screen, and Qt 6

## 3. Release checklist
- [ ] Version and `_COPYRIGHT_YEAR` in `configure.ac`
- [ ] Update checkpoints, assumevalid, minimumChainWork, chainTxData
- [ ] All unit tests pass, the functional test baseline passes (check that the list has not shrunk)
- [ ] Reproducible binary build with depends
- [ ] Release notes: whether consensus changes, activation heights, wallet compatibility (BDB version)
- [ ] Deployment on testnet first, followed by an observation period
