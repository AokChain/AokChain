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
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, wait_until

FREEZE = 70
STATE = ("utxo", "utxo_legacy", "tokens", "governance", "index")
REBUILT = "are built again from the block files at this start"
ASKED = "The databases of this node are not what they were"
TAKEN_UP = "was decided at an earlier run and is not done"
NOT_AGAIN = "were built again not long ago: nothing more is done about it"
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
        open(os.path.join(n1.datadir, "regtest", "aokchaind.pid"), "w").close()
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


if __name__ == '__main__':
    AutoRebuildTest().main()
