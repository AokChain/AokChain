# 19. Upgrading a node from 3.0.x to 4.0

For operators of staking nodes, exchange nodes and explorers. 4.0 has no hard fork: a 4.0
node and a 3.0.x node accept the same blocks and talk to each other. A node can be
upgraded on its own, at any time.

## Requirements

- Linux: Ubuntu 22.04 or later (glibc 2.35), x86_64. The archive
  `aokchain-4.0.0…-linux-x86_64.tar.gz` holds `aokchaind`, `aokchain-cli`, `aokchain-tx` and
  `aokchain-wallet`; nothing else has to be installed.
- The wallet file format is that of 3.0.x (Berkeley DB 4.8). A wallet can be opened by
  3.0.x again after 4.0 has used it (tested on regtest: 3.0.2 → 4.0 → 3.0.2).
- No reindex and no new download of the chain. The data directory is used as it is,
  with its indexes (`txindex`, `addressindex`, `spentindex`, `tokenindex`).

## Steps

```bash
sha256sum aokchain-4.0.0…-linux-x86_64.tar.gz          # compare with SHA256SUMS.txt
tar xzf aokchain-4.0.0…-linux-x86_64.tar.gz

sudo systemctl stop aokchaind                           # or: aokchain-cli stop
tail -1 ~/.aokchain/debug.log                           # must say "Shutdown: done"

cp -a ~/.aokchain/wallets ~/wallets-backup-$(date +%F)  # or ~/.aokchain/wallet.dat on old layouts

sudo mv /usr/local/bin/aokchaind    /usr/local/bin/aokchaind.old
sudo mv /usr/local/bin/aokchain-cli /usr/local/bin/aokchain-cli.old
sudo cp aokchain-4.0.0…-linux-x86_64/aokchaind aokchain-4.0.0…-linux-x86_64/aokchain-cli /usr/local/bin/

sudo systemctl start aokchaind
```

Wait for the process to end before copying or starting anything: while it shuts down the
process is no longer called `aokchaind`, so do not test for it by name. `systemctl stop`
waits; after `aokchain-cli stop`, wait until `~/.aokchain/aokchaind.pid` is gone.

Check, a minute later:

```bash
aokchain-cli getnetworkinfo | grep subversion      # "/AokChain:4.0.0/"
aokchain-cli getblockcount                         # the height of the other nodes
aokchain-cli getwalletinfo | grep -E '"balance"|immature|txcount'
aokchain-cli getstakinginfo | grep -E '"staking"|24h'
aokchain-cli repaircoins true                      # coins damaged by 3.0.x (see 16-stake-coin-time.md)
aokchain-cli repaircoins                           # repair them
```

Going back: stop the node, put the two old programs back, start it.

## Staking nodes

- **Damaged coins.** A node that has staked with 3.0.x has coins that lost their time
  (thousands, on a large staker). 4.0 stakes them correctly right away; `repaircoins`
  cleans the chain state. Staking continues at once: the coins keep their age.
- **Coin size.** The staking coins grow from 100–200 AOK to 1,000–2,000 AOK over the
  next days (`-stakecombinethreshold`), a dozen coins per stake.
- **Encrypt the staking wallet.** An unencrypted wallet file is all that someone needs to
  move the coins. With an encrypted wallet that is unlocked for staking only, the keys
  sign stakes and nothing else until the passphrase is given again:

  ```bash
  aokchain-cli encryptwallet "<passphrase>"                 # the node stops; start it again
  aokchain-cli walletpassphrase "<passphrase>" 0 true       # unlock for staking only, without a time limit
  ```

  With a timeout of 0 and `true` the wallet stays open for staking until it is locked or
  the node stops (a timed `walletpassphrase` for a payment returns to this state when its
  time is over). The unlock is lost when the node restarts: after every restart the second command is
  needed before the node stakes again. Do not put the passphrase into a script or the
  configuration file on the same server, which would undo the protection. Type it with a
  leading space, or use the RPC console, so that it does not stay in the shell history.
  Make a new backup after encrypting: the old backups are unencrypted.
- **Offline staking** keeps the spending key away from the server altogether
  (18-offline-staking.md). It needs 4.0 on the staking majority.

## Exchange nodes

- Nothing in the RPC interface that 3.0.x had is removed or changed: `sendrawtransaction`,
  `signrawtransaction`, `getrawtransaction`, `getblock`, the address index calls and the
  token calls answer as before. This was compared call by call on the explorer node (71
  requests of four services before and after the upgrade).
- New and useful for an exchange: `testmempoolaccept` (check a withdrawal before sending
  it), a fee rate limit for `sendrawtransaction`, `-rpcwhitelist` (an RPC user that may only
  call a list of methods), `scantxoutset` (the balance of a deposit address without the
  wallet).
- **Token deposits on 3.0.x.** 3.0.x does not check the sums of token amounts: the holder of
  one unit of a token can build a transaction that creates 2^64 of them. The same defect was
  published for Ravencoin in August 2026 (fixed there in 4.8.0). 4.0 refuses such a
  transaction, and with the stake majority on 4.0 it does not stay in the chain; but a 3.0.x
  node accepts it into its mempool and, for the time of a block that a 3.0.x staker makes,
  into its chain. An exchange that credits token deposits should run 4.0.
- An exchange node on 3.0.x keeps working beside 4.0 nodes. It should still be upgraded:
  3.0.x accepts blocks that break rules which 4.0 enforces (signatures, token amounts), and
  it can be stopped by a peer (SEC-03). As long as the stake majority runs 4.0, a block
  that breaks those rules does not stay in the chain.
- Stop deposits and withdrawals for the minutes of the upgrade, back up the wallet first,
  and compare the balance before and after.

## Explorers and services

- With `addressindex=1`, `spentindex=1`, `txindex=1` the node starts on the existing data.
- A node that is kept alive by a cron line (`* * * * * aokchaind -daemon`) restarts within
  a minute of a stop; replace the programs before stopping it, or use a systemd unit
  (`Type=forking`, `PIDFile=`, `ExecStop=aokchain-cli stop`, `TimeoutStopSec=600`).
  `-daemonwait` instead of `-daemon` makes the service count as started only when the node
  answers.
