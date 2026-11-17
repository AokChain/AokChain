AokChain Core
=============

AokChain Core is the full node of the AokChain network, with the wallet built into it. It
checks every block and transaction against the consensus rules itself. This directory holds
the documentation that ships with the source code. The guides for users are at
https://aok.network/doc/.

Getting it
----------

The release files and their SHA-256 hashes are listed at https://aok.network/download/ and
on the [GitHub release page](https://github.com/AokChain/AokChain/releases). Compare the hash
of the file you downloaded with `SHA256SUMS.txt` before you start it.

| Package | Contents |
|---|---|
| Windows 64-bit, wallet | `aokchain-qt.exe`: the desktop wallet, which is also a full node |
| Windows 64-bit, tools | `aokchaind.exe`, `aokchain-cli.exe`, `aokchain-tx.exe`, `aokchain-wallet.exe` |
| Linux x86_64 | `aokchaind`, `aokchain-cli`, `aokchain-tx`, `aokchain-wallet` (no desktop wallet); Ubuntu 22.04 or later |

Each package has a `README.txt` with what to check before the first start.

Running
-------

### Desktop wallet

Unpack the archive and start `aokchain-qt.exe`. The first start asks for the language and the
data folder, and then for a passphrase and a backup. See
[aokchain/14-first-run-and-wallet-security.md](aokchain/14-first-run-and-wallet-security.md).

### Node

```
./aokchaind -daemon
./aokchain-cli getblockchaininfo
./aokchain-cli stop
```

- Data directory: `~/.aokchain` on Linux, `%APPDATA%\AokChain` on Windows.
- Main network ports: 33441 for peers, 33440 for RPC (on this computer only, unless configured).
- A new node starts from a snapshot of the chain state whose hashes are compiled into the
  program and is at the tip within minutes. See [aokchain/15-snapshot.md](aokchain/15-snapshot.md)
  and [aokchain/12-bootstrap.md](aokchain/12-bootstrap.md).
- Coming from 3.0.x: [aokchain/19-upgrade-guide.md](aokchain/19-upgrade-guide.md). On Linux, the
  script made from `contrib/aokchain/upgrade.sh` installs or upgrades a node with one command
  (see `contrib/aokchain/README.md`).

Building from source
--------------------

[aokchain/02-build.md](aokchain/02-build.md) describes the build that the releases use, with
the unit and functional tests. The notes below come from the projects AokChain Core is derived
from and are kept for the platforms they cover; they may lag behind.

- [Dependencies](dependencies.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OS X Build Notes](build-osx.md)
- [OpenBSD Build Notes](build-openbsd.md)

Documents
---------

- [AokChain documents](aokchain/README.md): status, architecture, security audit, tests, the
  procedure for consensus changes, wallet, first sync, upgrade guide, changelog
- [Developer Notes](developer-notes.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPs](bips.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)
- [Assets Attribution](assets-attribution.md)

License
-------

Distributed under the [MIT software license](../COPYING).
This product includes software developed by the OpenSSL Project for use in the
[OpenSSL Toolkit](https://www.openssl.org/). This product includes cryptographic software
written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written
by Thomas Bernard.
