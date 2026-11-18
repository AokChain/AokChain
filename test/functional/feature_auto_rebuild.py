#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that a node rebuilds its databases by itself when it cannot go on from them.

- A start finds that the token database, the governance database or the chain
  state is not what the others say: the chain state, the token database and the
  governance database are built again from the block files at that start.
- A block that the node had validated before does not connect: the node notes
  that these databases are to be built again and stops with an error code; the
  next start, without any option, does it. Not twice within a day and not twice
  for the same block: then the block is taken to be invalid, as before.
- The block index database is damaged: the block files are indexed again, once.
- The same when the records of what the governance commands of its blocks replaced are gone.
- -autorebuild=0 turns all of this off.

The databases are damaged by removing them while the node is stopped, and the
chain state by removecoin (-regtest only).
"""

import os
import shutil
import subprocess
import time
from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes, connect_nodes_bi, disconnect_nodes, sync_blocks, wait_until

FREEZE = 70
STATE = ("utxo", "utxo_legacy", "tokens", "governance", "index")
REBUILT = "are built again from the block files at this start"
EARLIER = "was by an earlier version and did not end with a normal shutdown"
UNCLEAN = "did not end with a normal shutdown. Its databases are compared"
ASKED = "The databases of this node are not what they were"
TAKEN_UP = "was decided at an earlier run and is not done"
NOT_AGAIN = ": nothing more is done about it"
REINDEXED = "The block files are indexed again at this start"


def script_command(action, script_hex):
    """Data of a governance output: marker 'G', 'A', the action, and a script as its argument."""
    script = bytes.fromhex(script_hex)
    return (bytes([71, 65, action, len(script)]) + script).hex()


class AutoRebuildTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()
        return self.nodes[0].getbestblockhash()

    def count_in_log(self, text):
        with open(os.path.join(self.nodes[1].datadir, "regtest", "debug.log"), encoding="utf8") as log:
            return log.read().count(text)

    def count_in_stderr(self, text):
        self.stderr1.flush()
        with open(self.stderr1.name, encoding="utf8") as err:
            return err.read().count(text)

    def frozen(self, node):
        return sorted(a for a, is_frozen in node.freezelist()["addresses"].items() if is_frozen)

    def same_state(self):
        n0, n1 = self.nodes
        sync_blocks(self.nodes)
        hashes = [node.getstatehashes() for node in self.nodes]
        for part in STATE:
            assert_equal(hashes[0][part], hashes[1][part])
        assert_equal(self.frozen(n1), self.frozen(n0))
        assert_equal(len(self.frozen(n1)), 1)
        assert_equal(n1.gettokendata("REBUILD"), n0.gettokendata("REBUILD"))
        assert_equal(n1.listmytokens(), {"REBUILD": 100})

    def start1(self, extra_args=None):
        # (-checkmempool=0: the check of the memory pool against the chain state, which regtest
        # runs after every block, ends the node at a chain state that this test has damaged)
        self.start_node(1, self.args + ["-checkmempool=0"] + (extra_args or []), stderr=self.stderr1)
        connect_nodes_bi(self.nodes, 0, 1)

    def remove(self, name):
        shutil.rmtree(os.path.join(self.nodes[1].datadir, "regtest", name))

    def wait_exit(self, code):
        """Node1 ends by itself, with this exit code."""
        node = self.nodes[1]
        node.process.wait(timeout=120)
        assert_equal(node.process.returncode, code)
        node.running = False
        node.process = None
        node.rpc_connected = False
        node.rpc = None

    def damage_block(self):
        """A block that node1 has validated, and a chain state of node1 on which it does not connect."""
        n0, n1 = self.nodes
        coin = [u for u in n1.listunspent() if u["amount"] == 1000][0]
        raw = n1.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): Decimal("999.9")})
        n1.sendrawtransaction(n1.signrawtransactionwithwallet(raw)["hex"])
        self.sync_all()
        block = self.mine()
        n1.invalidateblock(block)                  # the coin is back in the chain state of node1...
        n1.removecoin(coin["txid"], coin["vout"])  # ...and is taken out of it
        return block

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        master = n0.getnewaddress()
        frozen_address = n1.getnewaddress()
        n0.sendmany("", {master: 100, frozen_address: 10})
        for _ in range(3):
            n0.sendtoaddress(n1.getnewaddress(), 1000)
        n0.sendtoaddress(n1.getnewaddress(), 50)  # (for the fees of node1's token transfers)
        self.mine()
        self.stop_nodes()
        self.args = ["-masteraddress=" + master]
        # (node1 says on its error output what it does about its databases: kept out of the test's)
        self.stderr1 = open(os.path.join(self.options.tmpdir, "node1.stderr"), "w")
        self.start_node(0, self.args)
        self.start1()
        n0, n1 = self.nodes

        # a frozen address and a token, so that the governance and the token database hold something
        coin = [u for u in n0.listunspent(1, 9999999, [master])][0]
        data = script_command(FREEZE, n0.validateaddress(frozen_address)["scriptPubKey"])
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {"data": data, master: coin["amount"] - Decimal("0.01")})
        n0.sendrawtransaction(n0.signrawtransactionwithwallet(raw)["hex"])
        self.mine()
        n0.issue("REBUILD", 1000, n0.getnewaddress(), "", 2, True)
        self.mine()
        n0.transfer("REBUILD", 100, n1.getnewaddress())
        self.mine(2)
        self.same_state()

        self.log.info("The token database is gone: the databases are built again at the start")
        self.stop_node(1)
        self.remove("tokens")
        self.start1()
        assert_equal(self.count_in_log(REBUILT), 1)
        self.same_state()

        self.log.info("The governance database is gone")
        self.stop_node(1)
        self.remove("governance")
        self.start1()
        assert_equal(self.count_in_log(REBUILT), 2)
        self.same_state()

        self.log.info("The chain state is gone")
        self.stop_node(1)
        self.remove("chainstate")
        self.start1()
        assert_equal(self.count_in_log(REBUILT), 3)
        self.same_state()

        self.log.info("An earlier version left the directory without a normal shutdown: the node makes sure once")
        # (an earlier version leaves no mark of its own while it runs, only its pid file)
        self.stop_node(1)
        with open(os.path.join(n1.datadir, "regtest", "aokchaind.pid"), "w") as f:
            f.write("4242\n")
        self.start1()
        assert_equal(self.count_in_log("was by an earlier version and did not end with a normal shutdown"), 1)
        assert_equal(self.count_in_log(REBUILT), 4)
        self.same_state()
        # this version's own unclean stop is answered by its notes, without a rebuild
        n1.process.kill()
        n1.process.wait()
        n1.running = False
        n1.process = None
        n1.rpc_connected = False
        n1.rpc = None
        self.start1()
        assert_equal(self.count_in_log("did not end with a normal shutdown. Its databases are compared"), 1)
        assert_equal(self.count_in_log(REBUILT), 4)
        self.same_state()

        self.log.info("A second instance on the directory of a running node ends at the lock and changes nothing")
        # (what the node does by itself, it does only as the one program that holds the lock of
        # the data directory: a second one never gets as far as looking at the pid file)
        pid_file = os.path.join(n1.datadir, "regtest", "aokchaind.pid")
        with open(pid_file) as f:
            assert_equal(int(f.read()), n1.process.pid)
        rebuilds = self.count_in_log(REBUILT)
        earlier = self.count_in_log("was by an earlier version and did not end with a normal shutdown")
        for extra in ([], ["-reindex-chainstate"], ["-reindex"]):
            second = subprocess.run(n1.args + self.args + extra, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            assert_equal(second.returncode, 1)
            assert "Cannot obtain a lock on data directory" in second.stderr.decode(), second.stderr
        with open(pid_file) as f:
            assert_equal(int(f.read()), n1.process.pid)
        assert_equal(self.count_in_log(REBUILT), rebuilds)
        assert_equal(self.count_in_log("was by an earlier version and did not end with a normal shutdown"), earlier)
        self.mine(2)
        self.same_state()

        self.log.info("-autorebuild=0: the node says what is wrong and does not start")
        self.stop_node(1)
        self.remove("tokens")
        n1.start(self.args + ["-checkmempool=0", "-autorebuild=0"], stderr=self.stderr1)
        self.wait_exit(1)
        assert_equal(self.count_in_log(REBUILT), 4)
        assert self.count_in_log("The token database does not belong to the chain state") >= 2
        self.start1()
        assert_equal(self.count_in_log(REBUILT), 5)
        self.same_state()

        self.log.info("A block that the node had validated does not connect: it stops, and the next start builds the databases again")
        block = self.damage_block()
        try:
            n1.reconsiderblock(block)
        except Exception:
            pass
        self.wait_exit(1)
        assert_equal(self.count_in_log(ASKED), 1)
        self.start1()
        assert_equal(self.count_in_log(TAKEN_UP), 1)
        assert_equal(n1.getbestblockhash(), block)
        self.same_state()

        self.log.info("Not twice within a day: the block is taken to be invalid, as before")
        block = self.damage_block()
        n1.reconsiderblock(block)
        assert_equal(self.count_in_log(NOT_AGAIN), 1)
        assert_equal(self.count_in_log(ASKED), 1)
        assert {"invalid"} == {tip["status"] for tip in n1.getchaintips() if tip["hash"] == block}

        self.log.info("A day later the node helps itself again")
        n1.setmocktime(int(time.time()) + 25 * 60 * 60)
        try:
            n1.reconsiderblock(block)
        except Exception:
            pass
        self.wait_exit(1)
        assert_equal(self.count_in_log(ASKED), 2)
        self.start1()
        assert_equal(self.count_in_log(TAKEN_UP), 2)
        self.same_state()

        self.log.info("The block index database is damaged: the block files are indexed again")
        marker = os.path.join(n1.datadir, "regtest", "autoreindex")
        index = os.path.join(n1.datadir, "regtest", "blocks", "index")

        def remove_table():
            tables = sorted(name for name in os.listdir(index) if name.endswith(".ldb"))
            assert tables
            os.remove(os.path.join(index, tables[0]))

        self.stop_node(1)
        remove_table()
        self.start1()
        assert_equal(self.count_in_log(REINDEXED), 1)
        wait_until(lambda: not os.path.exists(marker), timeout=60)
        self.same_state()

        self.log.info("Damaged again before that was finished: the node stops and says so")
        # (a start writes what the database has in its log into a table file)
        self.stop_node(1)
        self.start1()
        self.stop_node(1)
        remove_table()
        open(marker, "wb").close()
        n1.start(self.args + ["-checkmempool=0"], stderr=self.stderr1)
        self.wait_exit(1)
        assert_equal(self.count_in_log(REINDEXED), 1)
        assert_equal(self.count_in_log("it does not begin again"), 1)
        os.remove(marker)
        self.start1()
        assert_equal(self.count_in_log(REINDEXED), 2)
        wait_until(lambda: not os.path.exists(marker), timeout=60)
        self.same_state()

        self.log.info("The block index database is gone: the block files are indexed again, nothing is fetched")
        # (a chain state whose block the block index does not know: building the chain state
        # again from that block index would end at the first block)
        height = n1.getblockcount()
        reindexes = self.count_in_log(REINDEXED)
        self.stop_node(1)
        shutil.rmtree(index)
        n1.start(self.args + ["-checkmempool=0"], stderr=self.stderr1)   # (no peer: what it has, it has from its files)
        n1.wait_for_rpc_connection()
        wait_until(lambda: n1.getblockcount() == height, timeout=240)
        assert_equal(self.count_in_log(REINDEXED), reindexes + 1)
        assert_equal(n1.getconnectioncount(), 0)
        wait_until(lambda: not os.path.exists(marker), timeout=60)
        connect_nodes_bi(self.nodes, 0, 1)
        self.same_state()

        self.log.info("The records of what the governance commands of the blocks replaced are gone: the databases are built again")
        since = os.path.join(n1.datadir, "regtest", "governanceundo.since")
        rebuilds = self.count_in_log(REBUILT)
        self.stop_node(1)
        assert os.path.isfile(since)
        self.remove("governanceundo")
        self.start1()
        assert_equal(self.count_in_log("the database of what the governance commands of the blocks replaced is gone"), 1)
        assert_equal(self.count_in_log(REBUILT), rebuilds + 1)
        self.same_state()

        self.log.info("A data directory that never had them is taken as it is")
        taken_over = self.count_in_log("is on record for the blocks connected from now on")
        self.stop_node(1)
        self.remove("governanceundo")
        os.remove(since)
        self.start1()
        assert_equal(self.count_in_log(REBUILT), rebuilds + 1)
        assert_equal(self.count_in_log("is on record for the blocks connected from now on"), taken_over + 1)
        assert os.path.isfile(since)
        self.same_state()

        self.log.info("The block index database is gone on a node that keeps its records from a block on")
        # (a node that came from an earlier version by a restart, or from a snapshot: the block
        # from which on it keeps the records is in the lost block index as well)
        self.mine(3)
        with open(since) as f:
            assert_equal(f.read().strip(), n1.getblockhash(n1.getblockcount() - 3))
        height = n1.getblockcount()
        reindexes = self.count_in_log(REINDEXED)
        self.stop_node(1)
        shutil.rmtree(index)
        n1.start(self.args + ["-checkmempool=0"], stderr=self.stderr1)   # (no peer)
        n1.wait_for_rpc_connection()
        wait_until(lambda: n1.getblockcount() == height, timeout=240)
        assert_equal(self.count_in_log(REINDEXED), reindexes + 1)
        assert_equal(n1.getconnectioncount(), 0)
        wait_until(lambda: not os.path.exists(marker), timeout=60)
        connect_nodes_bi(self.nodes, 0, 1)
        self.same_state()

        self.log.info("The governance database is not what it was when the chain state was written: the note finds it")
        # The governance database is changed in place, with the chain state at the same block:
        # a block with a command that changes nothing is connected, and then given up the way
        # a version without the records gave it up, which releases the address. The chain
        # state of before, with its note, is then put beside that governance database.
        datadir1 = os.path.join(n1.datadir, "regtest")
        self.stop_node(1)
        chainstate_aside = os.path.join(self.options.tmpdir, "chainstate-of-node1")
        shutil.copytree(os.path.join(datadir1, "chainstate"), chainstate_aside)
        self.start1()
        coin = [u for u in n0.listunspent(1, 9999999, [master])][0]
        data = script_command(FREEZE, n0.validateaddress(frozen_address)["scriptPubKey"])
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {"data": data, master: coin["amount"] - Decimal("0.01")})
        n0.sendrawtransaction(n0.signrawtransactionwithwallet(raw)["hex"])
        block = self.mine()
        self.stop_node(1)
        self.remove("governanceundo")
        os.remove(since)
        self.start1()
        n1.invalidateblock(block)
        assert_equal(self.frozen(n1), [])
        assert_equal(len(self.frozen(n0)), 1)
        self.stop_node(1)
        shutil.rmtree(os.path.join(datadir1, "chainstate"))
        shutil.copytree(chainstate_aside, os.path.join(datadir1, "chainstate"))
        rebuilds = self.count_in_log(REBUILT)
        not_what_it_was = "the governance database is not what it was when the chain state was written"
        assert_equal(self.count_in_log(not_what_it_was), 0)
        self.start1()
        assert_equal(self.count_in_log(not_what_it_was), 1)
        assert_equal(self.count_in_log(REBUILT), rebuilds + 1)
        n1.reconsiderblock(block)
        self.same_state()

        self.log.info("This version's own stops are not taken for an earlier version's")
        pid_file = os.path.join(n1.datadir, "regtest", "aokchaind.pid")
        rebuilds = self.count_in_log(REBUILT)
        earlier = self.count_in_log(EARLIER)

        def unchanged():
            assert_equal(self.count_in_log(REBUILT), rebuilds)
            assert_equal(self.count_in_log(EARLIER), earlier)
            self.same_state()

        # killed at the tail of a normal shutdown: the databases are closed, the pid file is still there
        self.stop_node(1)
        self.start1(["-testcrashat=shutdown-tail"])
        n1.stop()
        self.wait_exit(0)
        assert os.path.isfile(pid_file)
        self.start1()
        unchanged()
        # killed when it has written its pid file, before it opens a database
        self.stop_node(1)
        n1.start(self.args + ["-checkmempool=0", "-testcrashat=pidfile"], stderr=self.stderr1)
        self.wait_exit(0)
        assert os.path.isfile(pid_file)
        self.start1()
        unchanged()
        # killed when it has set its mark of a running node
        unclean = self.count_in_log(UNCLEAN)
        self.stop_node(1)
        n1.start(self.args + ["-checkmempool=0", "-testcrashat=running-mark"], stderr=self.stderr1)
        self.wait_exit(0)
        self.start1()
        assert_equal(self.count_in_log(UNCLEAN), unclean + 1)
        unchanged()
        # killed, and its pid file as a forced stop of the machine can leave a file whose
        # content was not on disk yet: empty, or filled with something that is not a pid
        for content in (b"", b"\0\0\0\0\0\0"):
            unclean = self.count_in_log(UNCLEAN)
            n1.process.kill()
            self.wait_exit(-9)
            with open(pid_file, "wb") as f:
                f.write(content)
            self.start1()
            assert_equal(self.count_in_log(UNCLEAN), unclean + 1)
            unchanged()

        self.log.info("A pid file that is kept outside the data directory says nothing about the directory")
        outside = os.path.join(self.options.tmpdir, "elsewhere.pid")
        self.stop_node(1)
        with open(outside, "w") as f:
            f.write("4242\n")
        self.start1(["-pid=" + outside])
        unchanged()
        self.stop_node(1)
        assert not os.path.exists(outside)
        self.start1()
        unchanged()

        # A run that is killed leaves its pid file under the default name. The next run keeps
        # its pid file elsewhere and ends normally. The file of the killed run must not be
        # found, by the start after that, beside no note or beside the note of a later run.
        n1.process.kill()
        self.wait_exit(-9)
        assert os.path.isfile(pid_file)
        self.start1(["-pid=" + outside])
        assert not os.path.exists(pid_file)
        unchanged()
        self.stop_node(1)
        self.start1()
        unchanged()
        # ...and an earlier version's pid file under the default name is found by a start
        # that keeps its own elsewhere, answered once, and taken away
        self.stop_node(1)
        with open(pid_file, "w") as f:
            f.write("4242\n")
        self.start1(["-pid=" + outside])
        rebuilds += 1
        earlier += 1
        assert not os.path.exists(pid_file)
        unchanged()
        self.stop_node(1)
        self.start1(["-pid=" + outside])
        unchanged()
        self.stop_node(1)
        self.start1()
        unchanged()

        self.log.info("The pid file and its note, in the orders in which a stop can leave them")
        note = os.path.join(n1.datadir, "regtest", "pid.note")
        # 1. Stopped after the note is written and before the pid file: the note alone.
        #    Without a pid file no run is known to have ended badly: nothing is decided.
        self.stop_node(1)
        assert not os.path.exists(pid_file) and not os.path.exists(note)
        n1.start(self.args + ["-checkmempool=0", "-testcrashat=pidnote"], stderr=self.stderr1)
        self.wait_exit(0)
        assert os.path.isfile(note) and not os.path.exists(pid_file)
        self.start1()
        unchanged()
        #    The same after a run that was killed, whose pid file is still there with the pid
        #    of its note: that pid file must not be left beside the new note.
        n1.process.kill()
        self.wait_exit(-9)
        assert os.path.isfile(note) and os.path.isfile(pid_file)
        n1.start(self.args + ["-checkmempool=0", "-testcrashat=pidnote"], stderr=self.stderr1)
        self.wait_exit(0)
        assert os.path.isfile(note) and not os.path.exists(pid_file)
        self.start1()
        unchanged()
        # 2. Stopped after the pid file is written: both, with the same pid. This version's own
        #    stop (above: -testcrashat=pidfile, running-mark, shutdown-tail).
        # 3. Stopped at a shutdown after the pid file is removed and before the note is: the
        #    note alone again.
        self.stop_node(1)
        self.start1(["-testcrashat=shutdown-pid-removed"])
        n1.stop()
        self.wait_exit(0)
        assert os.path.isfile(note) and not os.path.exists(pid_file)
        # 4. An earlier version runs after that, the note of the stopped run still there.
        #    It ends normally and removes its pid file: the note alone, nothing is decided.
        self.start1()
        unchanged()
        #    It is killed: its pid file with its pid, beside a note with another pid. That
        #    pid file is not this version's: the unclean stop of the earlier version is found.
        self.stop_node(1)
        with open(note, "w") as f:
            f.write("4141 -\n")
        with open(pid_file, "w") as f:
            f.write("4242\n")
        self.start1()
        rebuilds += 1
        earlier += 1
        unchanged()
        #    (what is not seen: it was given the very pid of the note)
        self.stop_node(1)
        with open(note, "w") as f:
            f.write("4242 -\n")
        with open(pid_file, "w") as f:
            f.write("4242\n")
        self.start1()
        unchanged()

        self.log.info("A stop that is asked for while the node rebuilds by itself: it ends, and the next start goes on")
        # (the stop of a service manager, at three moments: the databases of the rebuild are
        # closed and not open again; its chain state is wiped and holds no block; the databases
        # are loaded and nothing is connected yet)
        for point in ("rebuild-begun", "rebuild-wiped", "loaded"):
            self.stop_node(1)
            self.remove("tokens")
            n1.start(self.args + ["-checkmempool=0", "-teststopat=" + point], stderr=self.stderr1)
            self.wait_exit(1)
            assert_equal(self.count_in_log("A stop is asked for at the point '%s'" % point), 1)
            self.start1()
            self.same_state()
        rebuilds = self.count_in_log(REBUILT)

        self.log.info("A database that cannot be opened while the databases are replaced: the start ends with its message")
        # (the token database of -reindex-chainstate, which is opened a second time, emptied)
        self.stop_node(1)
        cannot_open = self.count_in_stderr("Error opening block database")
        n1.start(self.args + ["-checkmempool=0", "-reindex-chainstate", "-testthrowat=tokens-reopen"], stderr=self.stderr1)
        self.wait_exit(1)
        assert_equal(self.count_in_stderr("Error opening block database"), cannot_open + 1)
        self.start1()
        self.same_state()
        rebuilds = self.count_in_log(REBUILT)

        self.log.info("An earlier version's unclean stop is not forgotten when the starts that find it are killed")
        self.stop_node(1)
        with open(pid_file, "w") as f:
            f.write("4242\n")
        # (a start that cannot write its note says so, ends, and leaves that pid file as it is)
        os.mkdir(note + ".new")
        cannot_write = self.count_in_stderr("Cannot write to data directory")
        n1.start(self.args + ["-checkmempool=0"], stderr=self.stderr1)
        self.wait_exit(1)
        assert_equal(self.count_in_stderr("Cannot write to data directory"), cannot_write + 1)
        os.rmdir(note + ".new")
        with open(pid_file) as f:
            assert_equal(f.read(), "4242\n")
        for point in ("pidfile", "running-mark"):
            n1.start(self.args + ["-checkmempool=0", "-testcrashat=" + point], stderr=self.stderr1)
            self.wait_exit(0)
        self.start1()
        assert_equal(self.count_in_log(EARLIER), earlier + 1)
        assert_equal(self.count_in_log(REBUILT), rebuilds + 1)
        self.same_state()
        # ...and it is answered once
        self.stop_node(1)
        self.start1()
        assert_equal(self.count_in_log(EARLIER), earlier + 1)
        assert_equal(self.count_in_log(REBUILT), rebuilds + 1)
        self.same_state()


        self.log.info("A validated block that does not connect, with more blocks behind it: none of them is left marked invalid")
        # node1 gives up block B for a branch of its own and loses the coin that B spends; node0
        # goes on from B with three blocks. When the nodes meet, node1 asks for its rebuild at
        # B, and the block messages that follow bring B again while the node is on its way down.
        # (Twice: with the check of the whole block index that -regtest runs after every step,
        # which must not end the node there, and without it, as on the main network.)
        for hours, check in ((60, []), (120, ["-checkblockindex=0"])):
            self.stop_node(1)
            n1.start(self.args + ["-checkmempool=0", "-persistmempool=0"] + check, stderr=self.stderr1)   # (no peer)
            n1.wait_for_rpc_connection()
            disconnect_nodes(n0, 1)
            # (a coin of node0: the wallet of node1 must not bring the payment back at its start)
            coin = [u for u in n0.listunspent() if u["amount"] > 2][0]
            raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): coin["amount"] - Decimal("0.1")})
            n0.sendrawtransaction(n0.signrawtransactionwithwallet(raw)["hex"])
            block_b = n0.generate(1)[0]
            n1.submitblock(n0.getblock(block_b, False))
            assert_equal(n1.getbestblockhash(), block_b)
            n1.invalidateblock(block_b)
            # (the payment of B is back in its memory pool: a start without it, so that its own
            # branch does not hold that payment)
            self.stop_node(1)
            n1.start(self.args + ["-checkmempool=0", "-persistmempool=0"] + check, stderr=self.stderr1)
            n1.wait_for_rpc_connection()
            assert_equal(n1.getrawmempool(), [])
            n1.generate(2)
            own_tip = n1.getbestblockhash()
            assert_equal(n1.removecoin(coin["txid"], coin["vout"]), True)
            n1.reconsiderblock(block_b)
            assert_equal(n1.getbestblockhash(), own_tip)
            n0.generate(3)
            asked, not_again, taken_up = self.count_in_log(ASKED), self.count_in_log(NOT_AGAIN), self.count_in_log(TAKEN_UP)
            n1.setmocktime(int(time.time()) + hours * 60 * 60)   # (the rebuild asked for before this one was within the day)
            try:
                connect_nodes(n1, 0)
            except Exception:
                pass
            self.wait_exit(1)
            assert_equal(self.count_in_log(ASKED), asked + 1)
            assert_equal(self.count_in_log(NOT_AGAIN), not_again)
            self.start1(check)
            assert_equal(self.count_in_log(TAKEN_UP), taken_up + 1)
            sync_blocks(self.nodes, timeout=240)
            assert_equal([tip["status"] for tip in n1.getchaintips() if tip["hash"] == n0.getbestblockhash()], ["active"])
            hashes = [node.getstatehashes() for node in self.nodes]
            for part in STATE:
                assert_equal(hashes[0][part], hashes[1][part])

        self.log.info("A store that is gone, and a node that does not rebuild by itself: no start goes on with a new one")
        # (the first start makes an empty store in its place; the second must not take that for the store of this chain)
        for name, said in (("governance", "The governance database does not belong to the chain state (it is new"),
                           ("tokens", "The token database does not belong to the chain state (it is empty")):
            rebuilds = self.count_in_log(REBUILT)
            before = self.count_in_log(said)
            self.stop_node(1)
            self.remove(name)
            for _ in range(2):
                n1.start(self.args + ["-checkmempool=0", "-autorebuild=0"], stderr=self.stderr1)
                self.wait_exit(1)
            assert_equal(self.count_in_log(said), before + 2)
            assert_equal(self.count_in_log(REBUILT), rebuilds)
            self.start1()
            assert_equal(self.count_in_log(REBUILT), rebuilds + 1)
            self.same_state()


if __name__ == '__main__':
    AutoRebuildTest().main()
