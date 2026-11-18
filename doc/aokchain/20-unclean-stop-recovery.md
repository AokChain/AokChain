# 20. What the node does after a stop without a normal shutdown

From 4.0.10. Source: `src/init.cpp` (the loop that loads the databases), `src/validation.cpp`
(`FlushStateToDisk`, `RecoverTokenState`, `VerifyStoresNote`, `DisconnectBlock`),
`src/governance/governance.cpp`, `src/tokens/tokendb.cpp`, `src/txdb.cpp`, `src/snapshot.cpp`.

## 1. The rule

After any stop that was not a normal shutdown (a kill, a crash, a forced stop of the virtual
machine, a loss of power), a plain start, without any option, ends in a node that runs and
whose databases belong to one block. Where the node cannot get there from what is on disk, it
builds its databases again by itself (`-autorebuild`, on by default). Nothing in this changes
which blocks or transactions are valid, no message of the network and no file of the wallet.
Going back to 4.0.9 after a normal stop is safe: what this version adds is either gone after
a normal stop or kept where 4.0.9 does not look.

## 2. The stores, and what keeps each of them at the block of the chain state

| Store | Written | Brought in step at a start |
|---|---|---|
| Block files, block index (`blocks/`) | synced at every write of the state, before the chain state | - |
| Chain state (`chainstate/`) | at a write of the state, synced | a write that was cut is finished (`ReplayBlocks`) |
| Governance (`governance/`) | with every block | a journal of what every commit replaced since the last write of the chain state is undone, back to the block of the chain state |
| Tokens (`tokens/`) | after the chain state, as one synced batch together with the block it stands for | the blocks from that block to the block of the chain state are applied again |
| Address, spent and timestamp index (optional) | with every block | a journal, as for governance |
| Governance records (`governanceundo/`) | synced, with every connected block that spends from the master address | - (read when a block is disconnected, see 6) |

The order of a write of the state: block and undo files and the block index, synced; the
governance database notes the write that follows, synced; the chain state, synced; the
governance journal is dropped; the token batch, synced; the note of 4.

A command that reads the databases itself (`listtokens`, `listtokenbalancesbyaddress`,
`listaddressesbytoken`, `gettxoutsetinfo`, the scans of unspent outputs) asks for such a write
first. When the databases hold everything already, and the chain state carries the mark of
this version, nothing is written.

## 3. What a start finds, and what it does about it

| Found | Step |
|---|---|
| The chain state is empty, or was written by a version without the marks | taken as it is |
| A journal that does not begin at the block of the chain state, or cannot be read | rebuild |
| The token database has no block that it stands for, or one that is not on the way to the block of the chain state | rebuild |
| The governance database is new (made at this start, or at an earlier one that ended on this), or the token database is empty, and the chain state holds more than the genesis block | rebuild |
| The governance records are gone (`governanceundo.since` is there, the database does not know of it) | rebuild |
| The note of 4 is there for the block of the chain state and does not match | rebuild |
| The last run was by an earlier version and did not end normally (see 5) | rebuild, once |
| The check of the last blocks fails | rebuild |
| The block index database is damaged, or does not know the block of the chain state (it is lost; also on a node that keeps its governance records from a block on, 6) | the block files are indexed again, once |
| While running: a block that this node had validated does not connect, or a block of its chain cannot be disconnected | the node stops with exit code 1 and rebuilds at the next start (not twice for one block, not twice within a day) |

A re-index (asked for with `-reindex`, or begun by the node) that a stop ends, whoever asked
for the stop, is not recorded as finished: the next start goes on with it. The same holds
for an import of `bootstrap.dat`, which is read again.

"Rebuild": the chain state, the token database, the governance database and the governance
records are built again from the block files, as `-reindex-chainstate` does (about 25 minutes
on the main network at height 3,031,432 on the build server). A stop during it is taken up at
the next start.

From the moment the node has asked for its rebuild, and after any other failure of its own
that ends it (a write that failed), it judges no block any more: it takes no new block (the
sender gets an error, not a verdict, and is not blamed), a block that does not connect gets
no failure mark, and a stop request ends the activation of the chain before its next step. A mark given in those last moments would be written with the block
index and outlive the rebuild.

A node that started from a snapshot and has not fetched the blocks before it cannot rebuild
or re-index. It starts over from the snapshot: the snapshot is fetched, checked and unpacked
beside the chain data, and only then takes their place. When the snapshot cannot be had, the
node ends with a message that says why and that nothing was removed, and tries again at every
start. A stop that is asked for while the snapshot is fetched ends the start without that
message. What happens when the unpacked snapshot cannot be put into its place, and with
databases that are kept on another file system, is in 15-snapshot.md §3.

A pruned node does none of this by itself. Where the others rebuild, it ends with the message
of the fault and "This node is pruned: ... Please restart with -reindex: the node then
downloads the blocks again." 4.0.9 started in these cases, with databases that did not belong
to one block. `-autorebuild=0` gives every node this behaviour (with `-reindex-chainstate` or
`-reindex` as the remedy). Two cases of the pruned node:

- Only the governance records are gone (`governanceundo/`). The message names two ways on:
  remove `governanceundo.since` and start again, and the node goes on with its databases and
  keeps the records from its current block on, as after an upgrade from a version that kept
  none (blocks below that one are then given up the way such a version did it, 6); or
  `-reindex`. The first is the operator's decision; the node does not take it.
- While it runs, a block of its chain cannot be disconnected (its record is missing), or a
  block that it had validated does not connect: the log says that the databases are not what
  they were and that `-reindex` is the way on. `invalidateblock` for such a block answers
  with an error and the block stays. A longer branch that asks for it stops the node; the
  check of the last blocks at the next start finds the missing record and the start ends
  with the message above.

A store that is gone is made anew, empty, when the node opens it. Such a store does not belong
to a chain state that holds blocks, whatever version wrote that chain state, and the node does
not go on with it: not at the start that made it, and not at a later one (the governance
database is marked as initialised only beside a chain state without blocks or by a rebuild; a
token database is never empty beside blocks).

`invalidateblock` and `reconsiderblock` can be given while a rebuild runs: the rebuild stops
before a block that is marked invalid, and goes on when the mark is taken away.

A node that ends on the signal SIGABRT writes a line about it to its debug log ("The program
ends here on the signal SIGABRT: one of its internal checks failed, or the signal was sent
to it from outside") and then ends by that signal, also when a watchdog or an operator sent
it. A check that failed is named on the standard error output; a node in the background
(`-daemon`) has the debug log for that, so the check, its file and its line are in the log
as well.

## 4. The note of the governance and the token database

An extra check, not what the rule of 1 rests on. At a write of the state that no command asked
for (a periodic one, one for a full cache, the one of a start, the one of a shutdown), the node
reads both databases from end to end and notes their hashes with the block. At a start, when
there is a note for the block of the chain state, both are read again and compared. The log
says which it was:

    The governance and the token database are what they were when the chain state was written (checked in 113ms)
    The governance and the token database are not checked against a note at this start: <why>

A write that a reading command asked for does not compute the note (on the main network it
costs a read of 25 MB under the lock of validation). After such a write, when it had
something to write, there is no note for that block until the next write of the other kind.
On a node that is queried all day the check is therefore mostly skipped after an unclean stop.
What the note adds is damage inside an existing database that the database's own checksums do
not report.

## 5. Whose unclean stop: the pid file and `pid.note`

A version before 4.0.10 keeps none of the marks of 2: after its unclean stop nothing can be
checked, so the node rebuilds once. Such a stop is known by the pid file that it leaves. This
version notes the pid in `pid.note` before it writes its pid file, and removes the note after
the pid file at a shutdown:

| Left on disk | It was | The next start |
|---|---|---|
| The note, no pid file | this version, stopped before it wrote the pid file or after it removed it | nothing to decide |
| A pid file and the note, same pid | this version, stopped at any time in between | its own marks deal with it; no rebuild for this reason |
| A pid file, no note, or a note with another pid | an earlier version wrote the pid file, after the last run of this version, and did not remove it | rebuild, once; kept in the note until it is answered |
| A pid file that holds no number (empty, or not a pid) | not known | nothing follows from it |

This version writes its pid file to disk before the start goes on, so a forced stop of the
machine does not leave its name without its content. A start that finds this version's own
pid file (the pid of the note), or one without a number, removes it before it writes its
note: at no moment does an old pid file of this version lie beside a note with the pid of a
later run. That holds for the file with the default name as well when a start keeps its pid
file elsewhere or under another name (`-pid`): the default-named file of an earlier run is
judged and taken away by that start too. It does not hold for a pid file under another name
inside the data directory (`-pid=other.pid`): a start looks at its own pid file and at the
default-named one, not at a file that an earlier run kept under a third name. The sequence
that still costs one needless rebuild (a start-over on a snapshot node): a run with
`-pid=other.pid` is killed; a run under another pid file name ends normally; a start with
`-pid=other.pid` then finds that file without a note and takes it for an earlier
version's. A node that always uses the same `-pid` setting does not meet it. A start that cannot write its note ("Cannot write to data directory") ends and
leaves the pid file of the last run as it found it, for the next start to judge.

Only a pid file in the data directory counts. Not seen: an earlier version's unclean stop when
the pid file is kept elsewhere (`-pid`) or removed by the service manager (`PIDFile=`); when
it had the very pid of the note that a killed run of this version left; and when a forced
stop of the machine in the first half minute of that earlier version's run left its pid file
empty (an earlier version did not write it to disk at once). The note of 4 is the check that
is left in these cases.

## 6. Governance commands of a block that is given up

When a block is connected, what its governance commands replaced is kept by block
(`governanceundo/`); when the block is disconnected, the governance state is put back from
it, exactly. A block that a version before 4.0.10 connected has no record: it is reverted
command by command, as before. The block from which on records are kept is noted in the record
database and in `governanceundo.since`. A block after it that spends from the master address
and has no record is not disconnected: the node stops and rebuilds. After a rebuild every
block has its record.

## 7. New in the data directory

`governanceundo/`, `governanceundo.since`, `pid.note` (while the node runs), `autoreindex`
(while a re-index that the node began runs), `snapshot-unpack/` (while a snapshot is
installed). A copy of a node is made of the whole data directory, or of `chainstate`,
`blocks`, `tokens` and `governance` alone; `governanceundo/` without `governanceundo.since`,
or the file without the directory, is not a state that the node leaves.

## 8. Tests

Functional (in the baseline): `feature_governance_unclean_stop.py`,
`feature_tokens_unclean_stop.py`, `feature_auto_rebuild.py`, `feature_governance_reorg.py`,
`feature_governance_pruned.py`, `feature_flush_for_read.py`, `feature_snapshot.py`,
`feature_stake_missing_coin.py`, and `feature_random_stops.py`, which stops a node at random
moments (kills; with `--powerhelper` forced stops that lose what was not synced) and compares
it after every plain restart with its peer and with a node that never gave a block up. A
round of it in which nothing was lost fails when the node had to help itself (a rebuild, a
run taken for an earlier version's) or had ended by itself. `feature_random_stops_fixed.py`
is the same test with a fixed seed and 22 rounds, with every kind of stop among them. For
tests only, on `-regtest`: `-testcrashat`, `-teststopat`, `-testfailtokenwrite`,
`-dbcrashtokenwrite`, `-tokencatchupstep`, `-testactivationpause`, `-testabortat`,
`-testthrowat`, `-testshutdownpause`, `-testabortimportat`, and the commands `removecoin` and `removegovernanceundo`. Further tests in the
baseline: `feature_invalidate_during_rebuild.py`, `feature_abort_message.py`.
