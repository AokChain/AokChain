# 19. Upgrading a node from 3.0.x to 4.0

For operators of staking nodes, exchange nodes and explorers. 4.0 has no hard fork: a 4.0
node and a 3.0.x node accept the same blocks and talk to each other. A node can be
upgraded on its own, at any time.

## Requirements

- Linux: Ubuntu 22.04 or later (glibc 2.34), x86_64. The archive
  `aokchain-4.0.0…-linux-x86_64.tar.gz` holds `aokchaind`, `aokchain-cli`, `aokchain-tx` and
  `aokchain-wallet`; nothing else has to be installed.
- The wallet file format is that of 3.0.x (Berkeley DB 4.8). A wallet can be opened by
  3.0.x again after 4.0 has used it (tested on regtest: 3.0.2 → 4.0 → 3.0.2).
- No reindex and no new download of the chain. The data directory is used as it is,
  with its indexes (`txindex`, `addressindex`, `spentindex`, `tokenindex`).

## Steps

The script `contrib/aokchain/upgrade.sh` does the steps below in one command
(`curl -fsSL <base url>/upgrade.sh | bash`): it checks the download, stops the node, keeps
the old programs and the wallet, installs, starts and runs `repaircoins`. It leaves an
existing `aokchain.conf` and the wallet as they are. By hand:

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

Going back: stop the node, put the two old programs back, start it. From 4.0.10 on, stop
the node normally before going back ("Between 4.0 versions" below).

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
- **Several wallets in one node.** With `staking=1` every wallet that the node has open
  stakes. Since 4.0.7 this includes a wallet that is opened later (`loadwallet`,
  `createwallet`, the wallet menu of the desktop wallet); up to 4.0.6 only the wallets of
  the start staked. A node that stakes with one wallet and keeps other wallets for a
  service names the wallets that stake:

  ```
  stakingwallet=<name as listwallets shows it>
  ```

  The default wallet has an empty name (`listwallets` shows `""`): it is named with
  `stakingwallet=` and nothing after the sign. `stakingwallet=wallet.dat` names no wallet,
  and nothing stakes.
  The line stands beside `staking=1` in the configuration file and may be given several
  times, once for every wallet that stakes. A wallet that is not named does not stake,
  whether it is opened at the start or later.
  Put the line into the configuration before the first start of 4.0.7 or later: a coin
  that has staked cannot be spent for 60 blocks, and a stake gathers the small coins of
  its address into one.

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

## Between 4.0 versions

A 4.0 node is upgraded to a later 4.0 version by the same steps: stop it normally, replace
the programs, start it. No change in the configuration file, nothing changes for wallets.
What 4.0.10 adds for the operator:

- **A start after a stop without a normal shutdown** (a kill, a crash, a power failure, a
  forced stop of the machine). Start the node as usual. The log says that the last run did
  not end with a normal shutdown and that the databases are compared with each other and
  brought in step: governance changes are undone and come back with their blocks, the token
  database is at an earlier height and the blocks in between are applied to it again, and
  with `-addressindex` the changes of the address index are undone in the same way. These
  lines are the repair, not an error.
- **The node rebuilds by itself.** At every start the node checks that its databases
  belong to one block. When it cannot go on from them (a store that is lost, damaged or
  cannot be brought in step, a failed check of the last blocks), the same start builds the
  chain state, the token database and the governance database again from the block files,
  as `-reindex-chainstate` does, and the log says so. This takes as long as a rebuild,
  some tens of minutes on the main network, and the node does not stake meanwhile; the
  wallet is not touched. A stop that is asked for during the rebuild ends the node
  normally, and the next start goes on. When the block index is damaged or lost, the
  block files are indexed again, as `-reindex` does.
- **A node that started from a chain state snapshot** and does not have the earlier
  blocks starts over from the snapshot instead; its wallet stays. Nothing is removed
  before the new snapshot is fetched, checked and unpacked. When the snapshot cannot be
  had, the node ends with a message that says why and that nothing was removed, and tries
  again at every start.
- **A block that the node had validated before and that does not connect any more** means
  that its databases are damaged. The node says "The databases of this node are damaged.
  They are built again from the block files at the next start: start the program again, no
  option is needed." and ends with exit code 1. Keep `Restart=on-failure` in the service
  unit: the node is then started again and rebuilds without a hand.
- **The first start after an earlier version.** A node that was stopped normally before
  the upgrade does not rebuild. A node that was killed or crashed under the earlier version
  left its pid file behind; the earlier version kept no notes that its databases could be
  checked against, so 4.0.10 rebuilds once at its first start. Only a pid file in the data
  directory counts: where it is kept elsewhere (`-pid`), where a service manager removes
  it (`PIDFile=`), and on Windows, which has no pid file, start 4.0.10 once with
  `-reindex-chainstate` if the earlier version was not stopped normally.
- **New in the data directory:** `governanceundo/` and `governanceundo.since` (what the
  governance commands of each block replaced, so that a block that leaves the chain is
  undone exactly) and, while the node runs, `pid.note`. A copy of a node is made of the
  whole data directory, or of `chainstate`, `blocks`, `tokens` and `governance` alone.
- **`-autorebuild=0`** turns the automatic steps off. The node then stops with the message
  that names the option to use, as before 4.0.10.
- **What stays with the operator:** errors of the disk (full, permissions, hardware), a
  block file that cannot be read, a changed index or prune option, and pruned nodes, which
  cannot rebuild from their block files: where another node rebuilds, a pruned node ends
  with the message of the fault, which names `-reindex`.
- The details are in `20-unclean-stop-recovery.md`.
- **`coins_missing` in `getstakinginfo`.** Above 0, the chain state of the node does not
  hold coins that the wallet would stake. The wallet goes on staking with its other coins,
  and the log names the coins. Rebuild the chain state at a convenient time
  (`-reindex-chainstate`); the wallet is not touched by it.
- **Memory of a rebuild.** A rebuild of the chain state or a `-reindex` needs far less
  memory than with 4.0.9 and before.
- **Address index.** With `-addressindex`, a rebuild of the chain state (`-reindex-chainstate`
  or the node's own) also empties and refills the address, spent and time indexes.
- **Going back to 4.0.9.** Stop the node normally, put the old programs back, start it. If
  the node was not stopped normally, start 4.0.10 once more, stop it normally, and only
  then put the old programs back; when that is not possible, rebuild the chain state
  after going back.
