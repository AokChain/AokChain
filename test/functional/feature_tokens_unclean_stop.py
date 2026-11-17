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

The token database now notes the block that it stands for. At startup the
token effects of the blocks between that block and the chain state are applied
again from the block files.

The write of the chain state is interrupted with -dbcrashratio, which stops the
node after a partial batch (-dbbatchsize=1: after the first record).
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, sync_mempools


class TokensUncleanStopTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()

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


if __name__ == '__main__':
    TokensUncleanStopTest().main()
