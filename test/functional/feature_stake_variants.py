#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the limit on stored blocks per stake (-maxblocksperstake).

One stake (a coin and a time) can sign any number of different blocks, and
each of them passes the checks made before a block is stored. A node stores
only a few blocks with the same stake; a further one is stored when a block
builds on it, so the node still follows whichever chain is extended.

node1 and node2 have the same key and the same single coin, and stake two
different blocks with it while partitioned. node0 stores one block per stake,
node4 has the default limit. node3 later stakes on top of node2's block.

 - both blocks have the same stake and are different blocks
 - node0 stores the first and leaves the second out, once (it does not
   download it again and again); node4 stores both
 - when node3 extends the second block, node0 fetches it with its child and
   reorganizes onto that chain
"""

import os
import time

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
    disconnect_nodes,
    sync_blocks,
    wait_until,
)

LAST_POW = 100
SLOT = 16
NOT_STORED = "is not stored:"


class StakeVariantsTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 5
        self.setup_clean_chain = True
        pos = ["-lastpowblock=%d" % LAST_POW]
        staker = pos + ["-staking=1", "-emergencystaking=1"]
        self.extra_args = [
            pos + ["-staking=0", "-maxblocksperstake=1"],
            staker,
            staker,
            pos + ["-staking=0"],
            pos + ["-staking=0"],
        ]

    def setup_network(self):
        self.setup_nodes()
        for i in range(1, self.num_nodes):
            connect_nodes_bi(self.nodes, 0, i)
        self.sync_all()

    def isolate(self):
        for i in range(self.num_nodes):
            for j in range(self.num_nodes):
                if i != j:
                    disconnect_nodes(self.nodes[i], j)

    def tip_status(self, node, blockhash):
        for tip in node.getchaintips():
            if tip["hash"] == blockhash:
                return tip["status"]
        return None

    def log_count(self, i, text):
        path = os.path.join(self.options.tmpdir, "node%d" % i, "regtest", "debug.log")
        with open(path, encoding="utf8", errors="replace") as log:
            return sum(1 for line in log if text in line)

    def stake_one(self, node, t):
        height = node.getblockcount()
        node.setmocktime(t)
        wait_until(lambda: node.getblockcount() > height, timeout=60)
        return node.getbestblockhash()

    def run_test(self):
        n0, n1, n2, n3, n4 = self.nodes
        t = n0.getblock(n0.getbestblockhash())["time"] + 600
        t -= t % SLOT
        for node in self.nodes:
            node.setmocktime(t)

        self.log.info("node1 and node2 share one key with one coin; node3 has a coin of its own")
        shared = n1.getnewaddress()
        n2.importprivkey(n1.dumpprivkey(shared))
        n0.generate(30)
        n0.sendtoaddress(shared, 100000)
        n0.sendtoaddress(n3.getnewaddress(), 100000)
        # two coins of node0 for the transactions that make the two blocks different
        spare = [n0.getnewaddress(), n0.getnewaddress()]
        txid = n0.sendmany("", {spare[0]: 10, spare[1]: 10})
        n0.generate(LAST_POW - 30)
        self.sync_all()
        assert_equal(n2.getbalance(), 100000)
        payments = []
        for address in spare:
            coin = [u for u in n0.listunspent(1, 9999999, [address]) if u["txid"] == txid][0]
            raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): 9})
            payments.append(n0.signrawtransactionwithwallet(raw)["hex"])

        self.log.info("Partitioned, node1 and node2 stake a block each with the same stake")
        self.isolate()
        n1.sendrawtransaction(payments[0])
        n2.sendrawtransaction(payments[1])
        # (the proof-of-work blocks ran ahead of the clock: start from the time of the tip)
        t = n0.getblock(n0.getbestblockhash())["time"] + 2 * SLOT
        t -= t % SLOT
        variant1 = self.stake_one(n1, t)
        variant2 = self.stake_one(n2, t)
        assert variant1 != variant2
        blocks = [n1.getblock(variant1), n2.getblock(variant2)]
        assert_equal(blocks[0]["time"], blocks[1]["time"])
        assert_equal(blocks[0]["height"], LAST_POW + 1)
        assert_equal(blocks[1]["height"], LAST_POW + 1)
        stakes = [node.getrawtransaction(block["tx"][1], True)["vin"][0] for node, block in ((n1, blocks[0]), (n2, blocks[1]))]
        assert_equal((stakes[0]["txid"], stakes[0]["vout"]), (stakes[1]["txid"], stakes[1]["vout"]))
        for node in (n0, n3, n4):
            node.setmocktime(t)

        self.log.info("node0 (one block per stake) stores the first block and leaves the second out")
        for observer, i in ((n0, 0), (n4, 4)):
            connect_nodes_bi(self.nodes, i, 1)
            wait_until(lambda: observer.getbestblockhash() == variant1, timeout=30)
            connect_nodes_bi(self.nodes, i, 2)
            wait_until(lambda: self.tip_status(observer, variant2) is not None, timeout=30)
        wait_until(lambda: self.log_count(0, NOT_STORED) == 1, timeout=30)
        wait_until(lambda: self.tip_status(n4, variant2) in ("valid-headers", "valid-fork"), timeout=30)
        # node2 still announces the block as its best: node0 does not ask for it again
        time.sleep(3)
        assert_equal(self.log_count(0, NOT_STORED), 1)
        assert_equal(self.log_count(4, NOT_STORED), 0)
        assert_equal(self.tip_status(n0, variant2), "headers-only")
        assert_equal(n0.getbestblockhash(), variant1)
        assert_equal(n4.getbestblockhash(), variant1)

        self.log.info("node3 stakes on top of the second block: node0 fetches both and follows")
        # (it takes the block before its staking is turned on, or it would stake a third block at that height)
        connect_nodes_bi(self.nodes, 3, 2)
        wait_until(lambda: n3.getbestblockhash() == variant2, timeout=30)
        self.stop_node(3)
        self.start_node(3, ["-lastpowblock=%d" % LAST_POW, "-staking=1", "-emergencystaking=1", "-mocktime=%d" % t])
        n3 = self.nodes[3]
        assert_equal(n3.getbestblockhash(), variant2)
        connect_nodes_bi(self.nodes, 3, 2)
        t += SLOT
        for node in (n0, n1, n2, n4):
            node.setmocktime(t)
        child = self.stake_one(n3, t)
        assert_equal(n3.getblock(child)["previousblockhash"], variant2)
        for node in (n2, n0, n4, n1):
            wait_until(lambda: node.getbestblockhash() == child, timeout=60)
        assert_equal(self.tip_status(n0, variant1), "valid-fork")
        assert_equal(self.log_count(0, NOT_STORED), 1)
        sync_blocks(self.nodes)


if __name__ == '__main__':
    StakeVariantsTest().main()
