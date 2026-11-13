# AokChain development documentation

This is the collection of internal documents for the work on the new AokChain (feature upgrades, security fixes, stability improvements).
The upstream Bitcoin Core documentation is in the parent `doc/` directory; this directory covers only what is specific to AokChain.

| Document | Contents |
|---|---|
| [01-status.md](01-status.md) | Current status, change history, next steps |
| [02-build.md](02-build.md) | How to build and test on the build server, and things to watch out for |
| [03-architecture.md](03-architecture.md) | Chain parameters, PoS, transaction and block formats, tokens, governance, library layering |
| [04-security-audit.md](04-security-audit.md) | Security audit findings (SEC-xx), severity, remediation and status |
| [05-stability-testing.md](05-stability-testing.md) | State of the unit and functional tests, classification of failures, warnings, runtime checks |
| [06-roadmap.md](06-roadmap.md) | Phased roadmap, **consensus change procedure**, release checklist |
| [07-upstream-backports.md](07-upstream-backports.md) | List of upstream changes to adopt from Bitcoin Core / Ravencoin / PoS chains — prioritized by whether a fork is needed |
| [08-wallet-performance.md](08-wallet-performance.md) | The wallet slows down as transactions accumulate — cause, measurements, fixes, remaining bottlenecks |
| [09-mnemonic.md](09-mnemonic.md) | Extracting private keys from the mnemonic (recovery words) of another wallet — `mnemonictokeys`, `mnemonicfindaddress` |
| [10-gui-theme.md](10-gui-theme.md) | GUI — sidebar shell, light theme, fonts (Noto Sans / KR / Mono), token logos, shortened addresses, explorer |
| [11-multisig.md](11-multisig.md) | Signing and multisig GUI — proving my address and checking a signature, making and adding a multisig wallet, sending with a signing request, the text formats that are exchanged, storage, checks |
| [12-bootstrap.md](12-bootstrap.md) | Fast first synchronization — assumevalid update, block bootstrap (block archives fetched over HTTP(S) or from a folder and validated in full), format, options, creating and publishing, measurements |
| [13-old-version-cutoff.md](13-old-version-cutoff.md) | Old version cutoff — a mechanism that stops connecting to 3.0.x peers from a set height on. Currently disabled |
| [14-first-run-and-wallet-security.md](14-first-run-and-wallet-security.md) | First-run wizard and mandatory wallet encryption, staking-only unlock, restore from recovery words (`importmnemonic`), settings and address book changes, send confirmation |
| [15-snapshot.md](15-snapshot.md) | Chain state snapshot — a new node starts from the state at a fixed height whose hashes are compiled into the program; trust model, what a snapshot node can do without the past blocks, fetching them on demand, making a snapshot, faster block validation, measurements, consensus notes |
| [16-stake-coin-time.md](16-stake-coin-time.md) | Coins lost the time of their transaction in a reorganization and then produced stake blocks that every node rejects; what was observed on the main network, the fix in staking and in block disconnection, the `repaircoins` RPC |
| [17-upstream-features.md](17-upstream-features.md) | Features taken over from later Bitcoin Core versions: the plan in four phases, and phase 1 — `unloadwallet`, `scantxoutset`, `testmempoolaccept`, the fee rate limit of `sendrawtransaction`, `getaddressinfo`, `getbalances`, `-rpcwhitelist`, `-daemonwait`, the log rate limit and others |
| [18-offline-staking.md](18-offline-staking.md) | Offline staking: coins that one wallet owns and another stakes; how to use it; the validation defect that let the staking key take or destroy delegated coins and the rule that closes it; limits |
| [19-upgrade-guide.md](19-upgrade-guide.md) | Upgrading a node from 3.0.x to 4.0: steps, checks and going back; what a staking node, an exchange node and an explorer need to know; encrypting a staking wallet and the staking-only unlock |
| [release-notes-4.0.0.md](release-notes-4.0.0.md) | Release notes of 4.0.0 (candidate) |
| [CHANGELOG-4.0.0.md](CHANGELOG-4.0.0.md) | Changes compared with 3.0.5 |
| [functional-baseline.txt](functional-baseline.txt) | List of functional tests that currently pass (for regression checks) |

## Rules for maintaining the documents
- When a security issue is fixed, change its status in document 04 and record the commit hash.
- When tests are fixed, update the numbers in document 05 and `functional-baseline.txt`.
- When chain parameters or consensus change, update document 03.
- When a phase is finished, update the commit history in document 01 and the checkboxes in document 06.
