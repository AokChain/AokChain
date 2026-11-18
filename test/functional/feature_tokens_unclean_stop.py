#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that the token database follows the chain state over an interrupted write.

The token cache is written to the token database after the chain state (the
coins database). A node that stops in between, or in the middle of a write of
the chain state that is finished at the next start (ReplayBlocks), came back
with the chain state of the new block and the token database of the block of
the write before: tokens issued, reissued and transferred in between were
gone from it, without a sign.

The token database is now written as one batch together with the block that it
stands for. At startup the token effects of the blocks between that block and
the chain state are applied again from the block files, in steps when the way
is long, and a stop on that way is taken up again at the next start.

The write of the chain state is interrupted with -dbcrashratio, which stops the
node after a partial batch (-dbbatchsize=1: after the first record). The write
of the token database is interrupted with -dbcrashtokenwrite=<n>, which stops
the node where its n-th write of token records would begin (-regtest only).
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes_bi, disconnect_nodes, sync_blocks, sync_mempools, wait_until


class TokensUncleanStopTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()

    def count_in_log(self, node, text):
        with open(node.datadir + "/regtest/debug.log", encoding="utf8") as log:
            return log.read().count(text)

    def same_state(self):
        hashes = [node.getstatehashes() for node in self.nodes]
        assert_equal(hashes[0]["tokens"], hashes[1]["tokens"])
        assert_equal(hashes[0]["utxo"], hashes[1]["utxo"])
        assert_equal(hashes[0]["governance"], hashes[1]["governance"])

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        # (a transfer of tokens pays its network fee in coins: both wallets need some)
        n0.sendtoaddress(n1.getnewaddress(), 1000000)
        self.mine()
        address0 = n0.getnewaddress()
        address1 = n1.getnewaddress()

        self.log.info("A token from before the interrupted write")
        n0.issue("BEFORE", 1000, address0, "", 4, True)
        self.mine()
        self.same_state()

        self.log.info("Token activity that node1 connects but has not written")
        # node1's next write of the chain state stops after its first record
        self.stop_node(1)
        self.start_node(1, ["-dbcrashratio=1", "-dbbatchsize=1"])
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        n0.issue("CRASHCOIN", 500, address0, "", 2, True)
        self.mine()
        n0.transfer("CRASHCOIN", 120, address1)
        n0.reissue("BEFORE", 250, address0)
        self.mine()
        n0.issueunique("CRASHCOIN", ["one"])
        n0.transfer("BEFORE", 10, address1)
        self.mine()
        names = ["BEFORE", "CRASHCOIN", "CRASHCOIN#one"]
        expected = {name: n0.gettokendata(name) for name in names}
        mine = {"BEFORE": 10, "CRASHCOIN": 120}
        assert_equal({name: n1.gettokendata(name) for name in names}, expected)
        assert_equal(n1.listmytokens(), mine)
        tip = n0.getbestblockhash()

        self.log.info("The write of the chain state on node1 is interrupted")
        try:
            n1.gettxoutsetinfo()
            raise AssertionError("the node was to stop in the write")
        except AssertionError:
            raise
        except Exception:
            pass
        n1.wait_until_stopped()
        with open(n1.datadir + "/regtest/debug.log", encoding="utf8") as log:
            assert "Simulating a crash" in log.read()

        self.log.info("The next start finishes it; the token database follows")
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        sync_blocks(self.nodes)
        assert_equal(n1.getbestblockhash(), tip)
        with open(n1.datadir + "/regtest/debug.log", encoding="utf8") as log:
            assert "Replaying blocks" in log.read()
        assert_equal({name: n1.gettokendata(name) for name in names}, expected)
        assert_equal(n1.listmytokens(), mine)
        assert_equal(n1.listtokenbalancesbyaddress(address1), mine)
        self.same_state()

        self.log.info("The chain goes on with the tokens of the interrupted write")
        n1.transfer("CRASHCOIN", 20, n0.getnewaddress())
        sync_mempools(self.nodes)
        n0.reissue("CRASHCOIN", 30, address0)
        self.mine()
        assert_equal(n1.listmytokens()["CRASHCOIN"], 100)
        assert_equal(n1.gettokendata("CRASHCOIN")["amount"], 530)
        self.same_state()

        self.log.info("A clean stop and start changes nothing")
        self.stop_node(1)
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        self.mine()
        assert_equal(n1.listmytokens(), {"BEFORE": 10, "CRASHCOIN": 100})
        self.same_state()

        self.log.info("The write of the token database itself is interrupted")
        self.stop_node(1)
        self.start_node(1, ["-dbcrashtokenwrite=1"])
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        n0.issue("INSIDE", 77, address0, "", 0, True)
        self.mine()
        n0.transfer("INSIDE", 7, address1)
        n0.transfer("CRASHCOIN", 5, address1)
        self.mine()
        n0.reissue("BEFORE", 50, address0)
        n0.issueunique("CRASHCOIN", ["two"])
        self.mine()
        names = ["BEFORE", "CRASHCOIN", "CRASHCOIN#one", "CRASHCOIN#two", "INSIDE"]
        expected = {name: n0.gettokendata(name) for name in names}
        mine = {"BEFORE": 10, "CRASHCOIN": 105, "INSIDE": 7}
        assert_equal(n1.listmytokens(), mine)
        tip = n0.getbestblockhash()
        try:
            n1.gettxoutsetinfo()
            raise AssertionError("the node was to stop in the write")
        except AssertionError:
            raise
        except Exception:
            pass
        n1.wait_until_stopped()
        assert_equal(self.count_in_log(n1, "Simulating a crash in the write of the token database"), 1)

        self.log.info("The repair at the next start is interrupted twice: each start goes on where the last one got to")
        # (three blocks behind; a step for every block; the second write of a run stops the node)
        repairs = self.count_in_log(n1, "Tokens: the token database is at height")
        for crashes in (2, 3):
            n1.start(["-tokencatchupstep=1", "-dbcrashtokenwrite=2"])
            n1.wait_until_stopped()
            assert_equal(self.count_in_log(n1, "Simulating a crash in the write of the token database"), crashes)
        self.start_node(1, ["-tokencatchupstep=1"])
        n1 = self.nodes[1]
        assert_equal(self.count_in_log(n1, "Tokens: the token database is at height"), repairs + 3)
        # (the chain state had been written: the node is at the tip before it has a peer)
        assert_equal(n1.getbestblockhash(), tip)
        assert_equal({name: n1.gettokendata(name) for name in names}, expected)
        assert_equal(n1.listmytokens(), mine)
        # (node1 sent CRASHCOIN on above: what is left of the first amount is on a change address)
        assert_equal(n1.listtokenbalancesbyaddress(address1), {"BEFORE": 10, "CRASHCOIN": 5, "INSIDE": 7})
        for address in (address0, address1):
            assert_equal(n1.listtokenbalancesbyaddress(address), n0.listtokenbalancesbyaddress(address))
        connect_nodes_bi(self.nodes, 0, 1)
        self.same_state()

        self.log.info("The chain goes on, and a normal stop and start changes nothing")
        n1.transfer("INSIDE", 3, n0.getnewaddress())
        sync_mempools(self.nodes)
        self.mine()
        self.stop_node(1)
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        self.mine()
        assert_equal(n1.listmytokens(), {"BEFORE": 10, "CRASHCOIN": 105, "INSIDE": 4})
        assert_equal(self.count_in_log(n1, "Tokens: the token database is at height"), repairs + 3)
        self.same_state()

        self.log.info("A write of the token database that fails: the database is not said to stand for a block whose changes it lacks")
        repairs = self.count_in_log(n1, "Tokens: the token database is at height")
        self.stop_node(1)
        stderr1 = open(self.options.tmpdir + "/node1.stderr", "w")
        # (-testshutdownpause: the node stays up for three seconds after it has asked for its
        # stop, so that the block below reaches it on its way down at every run)
        self.start_node(1, ["-testfailtokenwrite=1", "-testshutdownpause=3000"], stderr=stderr1)
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        n0.issue("FAILED", 5, address0, "", 0, True)
        self.mine()
        n0.transfer("INSIDE", 1, address1)
        self.mine()
        # A block that moves the token issued since the last write, kept from node1 for now.
        # After the write that fails, the token is neither in what node1 holds in memory (it
        # was handed to the write) nor in its database: by that, the block would be invalid.
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        n0.transfer("FAILED", 2, address1)
        n0.generate(1)
        late_block = n0.getbestblockhash()
        late_hex = n0.getblock(late_block, False)
        try:
            n1.gettxoutsetinfo()   # (asks for a write of the state: the write of the tokens fails, the node stops)
        except Exception:
            pass
        # ...and the block reaches the node while it is on its way down: it is not taken, and
        # the answer is an error, not a verdict on the block
        assert_raises_rpc_error(-25, "the node is stopping after a failure of its own", n1.submitblock, late_hex)
        n1.wait_until_stopped()
        assert_equal(self.count_in_log(n1, "Simulating a write of the token database that fails"), 1)
        # (a node that is on its way down after a write that failed connects no further block)
        assert_equal(self.count_in_log(n1, "UpdateTip: new best=%s" % late_block), 0)
        # (its last write, at the shutdown, went through: the token database must not have been
        # marked with the tip by it)
        self.start_node(1, stderr=stderr1)
        connect_nodes_bi(self.nodes, 0, 1)
        n1 = self.nodes[1]
        assert_equal(self.count_in_log(n1, "Tokens: the token database is at height"), repairs + 1)
        # (no block was judged by what the node held after the failed write: none is marked invalid)
        assert_equal([tip for tip in n1.getchaintips() if tip["status"] == "invalid"], [])
        self.mine()
        assert_equal(n1.getbestblockhash(), n0.getbestblockhash())
        assert_equal(n1.gettokendata("FAILED"), n0.gettokendata("FAILED"))
        assert_equal(n1.listmytokens(), {"BEFORE": 10, "CRASHCOIN": 105, "FAILED": 2, "INSIDE": 5})
        self.same_state()

        self.log.info("A re-index that ends because the node stops after a failure of its own is not recorded as finished")
        height = n1.getblockcount()
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        self.stop_node(1)
        finished = self.count_in_log(n1, "Reindexing finished")
        # (the node gets on its way down when twenty blocks of the block file are read, and
        # its threads go on for three seconds: time for the import thread to run on, if it would)
        self.nodes[1].start(["-reindex", "-connect=0", "-testabortimportat=20", "-testshutdownpause=3000"], stderr=stderr1)
        # (exit code 1 when the stop came before the start was through, else 0)
        assert self.nodes[1].process.wait(timeout=240) in (0, 1)
        self.nodes[1].running = False
        self.nodes[1].process = None
        assert_equal(self.count_in_log(n1, "The node is stopped in the middle of an import"), 1)
        assert_equal(self.count_in_log(n1, "Reindexing finished"), finished)
        # the next start goes on with the re-index, without a peer, to the end of its block files
        self.start_node(1, ["-connect=0"], stderr=stderr1)
        n1 = self.nodes[1]
        wait_until(lambda: n1.getblockcount() == height, timeout=240)
        assert_equal(self.count_in_log(n1, "Reindexing finished"), finished + 1)
        connect_nodes_bi(self.nodes, 0, 1)
        self.same_state()
        stderr1.close()


if __name__ == '__main__':
    TokensUncleanStopTest().main()
