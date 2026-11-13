#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test a freeze command and a payment from the address it freezes in the same mempool.

The payment entered the mempool before the freeze command; both are valid on their
own, but not in this order in one block: a block that has the freeze command first
and the payment after it is invalid. Block assembly put both in one block, the
command first when its fee rate was higher, so a node made no valid block for as
long as both were in its mempool (a staker made invalid ones). The owner of the
address only had to send a payment with a smaller fee than the freeze command.

 - proof of work, the freeze command pays more: the block takes it and leaves the
   payment out; the payment is then removed from the mempool (it can never be in
   a block now)
 - the payment pays more: the block takes the payment, the freeze command waits
   for the next block
 - proof of stake: the staker keeps making blocks with the freeze command in them
"""

from decimal import Decimal

from feature_governance import FREEZE, script_command
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, wait_until

LAST_POW = 110
SLOT = 16


class GovernanceMempoolTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-lastpowblock=%d" % LAST_POW]] * 2

    def script_of(self, address):
        return self.nodes[0].validateaddress(address)["scriptPubKey"]

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()

    def command(self, address, data, fee):
        n0 = self.nodes[0]
        coin = [u for u in n0.listunspent(1, 9999999, [address]) if u["amount"] > 1][0]
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {"data": data, address: coin["amount"] - fee})
        txid = n0.sendrawtransaction(n0.signrawtransactionwithwallet(raw)["hex"])
        self.sync_all()
        return txid

    def payment(self, address, fee):
        n1 = self.nodes[1]
        coin = n1.listunspent(1, 9999999, [address])[0]
        raw = n1.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n1.getnewaddress(): coin["amount"] - fee})
        txid = n1.sendrawtransaction(n1.signrawtransactionwithwallet(raw)["hex"])
        self.sync_all()
        return txid

    def in_block(self, txid):
        return txid in self.nodes[0].getblock(self.nodes[0].getbestblockhash())["tx"]

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        master = n0.getnewaddress()
        victims = [n1.getnewaddress() for _ in range(3)]
        outputs = {master: 30000}
        for victim in victims:
            outputs[victim] = 1000
        n0.sendmany("", outputs)
        # coins of node0 at several addresses, to stake with
        for _ in range(10):
            n0.sendtoaddress(n0.getnewaddress(), 100000)
        self.mine()
        self.stop_nodes()
        self.extra_args = [["-lastpowblock=%d" % LAST_POW, "-masteraddress=" + master]] * 2
        self.start_nodes(self.extra_args)
        connect_nodes_bi(self.nodes, 0, 1)
        n0, n1 = self.nodes
        self.mine()

        self.log.info("The freeze command pays more: the block takes it, the payment is left out")
        spend = self.payment(victims[0], Decimal("0.0001"))
        freeze = self.command(master, script_command(FREEZE, self.script_of(victims[0])), Decimal("0.01"))
        assert_equal(sorted(n0.getrawmempool()), sorted([spend, freeze]))
        self.mine()
        assert self.in_block(freeze)
        assert not self.in_block(spend)
        assert_equal(n0.checkfreeze(victims[0]), True)
        # the payment can never be in a block: it is removed from the mempool
        for node in self.nodes:
            assert_equal(node.getrawmempool(), [])

        self.log.info("The payment pays more: the block takes it, the freeze command comes in the next block")
        spend = self.payment(victims[1], Decimal("0.05"))
        freeze = self.command(master, script_command(FREEZE, self.script_of(victims[1])), Decimal("0.01"))
        self.mine()
        assert self.in_block(spend)
        assert not self.in_block(freeze)
        assert_equal(n0.checkfreeze(victims[1]), False)
        assert_equal(n0.getrawmempool(), [freeze])
        self.mine()
        assert self.in_block(freeze)
        assert_equal(n0.checkfreeze(victims[1]), True)

        self.log.info("Proof of stake: the staker goes on making blocks")
        n0.generate(LAST_POW - n0.getblockcount())
        self.sync_all()
        assert_equal(n0.getblockcount(), LAST_POW)
        self.stop_nodes()
        self.start_nodes([self.extra_args[0] + ["-staking=1", "-emergencystaking=1"], self.extra_args[1]])
        connect_nodes_bi(self.nodes, 0, 1)
        n0, n1 = self.nodes
        # the clock at the time of the tip: no slot for a new block yet, so that both
        # transactions are in the mempool before the staker can make a block
        t = n0.getblock(n0.getbestblockhash())["time"]
        for node in self.nodes:
            node.setmocktime(t)
        spend = self.payment(victims[2], Decimal("0.0001"))
        freeze = self.command(master, script_command(FREEZE, self.script_of(victims[2])), Decimal("0.01"))
        assert_equal(n0.getblockcount(), LAST_POW)
        t -= t % SLOT
        for _ in range(3):
            height = n0.getblockcount()
            t += SLOT
            for node in self.nodes:
                node.setmocktime(t)
            wait_until(lambda: n0.getblockcount() > height, timeout=60)
        sync_blocks(self.nodes)
        assert n0.getblockcount() >= LAST_POW + 3
        assert_equal(n0.checkfreeze(victims[2]), True)
        assert spend not in n0.getrawmempool()
        assert_equal(n0.getrawtransaction(freeze, True)["confirmations"] > 0, True)
        # the payment was in no block, and is in no mempool
        assert spend not in n1.getrawmempool()
        assert n1.gettransaction(spend)["confirmations"] <= 0


if __name__ == '__main__':
    GovernanceMempoolTest().main()
