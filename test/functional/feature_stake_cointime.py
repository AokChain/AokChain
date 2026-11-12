#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that a coin keeps the time of its transaction through a reorganization.

The undo data of a block has neither the time nor the coinstake mark of a spent
coin. Versions before 4.0 gave the coins of a disconnected block back with time
0. The staking code hashed the kernel with the time of the coin, validation with
the time of the transaction: a wallet whose stake had been orphaned once then
produced blocks from that coin that every node rejected, and the coin never
staked again.

 - a node that reorganizes away from its own stakes has the same chain state,
   field by field, as a node that never had those blocks
 - a node that lost the times (-restorecointime=0, the old behaviour) still
   stakes such a coin: the staking code reads the time from the transaction
 - repaircoins finds the damaged coins and repairs them
"""

import os

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
BASE = ["-lastpowblock=%d" % LAST_POW]
STAKING = ["-staking=1", "-emergencystaking=1"]
OLD = ["-restorecointime=0"]


class StakeCoinTimeTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [BASE + STAKING, BASE + STAKING, BASE + STAKING + OLD]

    def connect_all(self):
        for a, b in ((0, 1), (0, 2), (1, 2)):
            connect_nodes_bi(self.nodes, a, b)

    def disconnect_all(self):
        for a, b in ((0, 1), (0, 2), (1, 2)):
            disconnect_nodes(self.nodes[a], b)
            disconnect_nodes(self.nodes[b], a)

    def setup_network(self):
        self.setup_nodes()
        self.connect_all()

    def stake(self, nodes, t):
        heights = [node.getblockcount() for node in nodes]
        for node in nodes:
            node.setmocktime(t)
        for node, height in zip(nodes, heights):
            wait_until(lambda: node.getblockcount() > height, timeout=60)

    def kernel_of(self, node, blockhash):
        block = node.getblock(blockhash, 2)
        assert "proof-of-stake" in block["flags"]
        vin = block["tx"][1]["vin"][0]
        return (vin["txid"], vin["vout"])

    def log_of(self, i):
        with open(os.path.join(self.nodes[i].datadir, "regtest", "debug.log"), errors="replace") as f:
            return f.read()

    def restart(self, i, args, t):
        self.stop_node(i)
        self.start_node(i, BASE + args + ["-mocktime=%d" % t])

    def run_test(self):
        n0, n1, n2 = self.nodes
        t = n0.getblock(n0.getbestblockhash())["time"] + 600
        t -= t % SLOT
        for node in self.nodes:
            node.setmocktime(t)

        self.log.info("Proof-of-work phase; node1 gets several coins, node2 exactly two")
        n0.generate(30)
        for _ in range(8):
            n0.sendtoaddress(n1.getnewaddress(), 100000)
        for _ in range(2):
            n0.sendtoaddress(n2.getnewaddress(), 100000)
        n0.generate(LAST_POW - 30)
        self.sync_all()
        assert_equal(n2.getblockcount(), LAST_POW)

        self.log.info("node0 and node2 stake two blocks each on their own, node1 a longer chain")
        self.disconnect_all()
        start = n0.getblock(n0.getbestblockhash())["time"] + 2 * SLOT
        start -= start % SLOT
        t = start
        kernels = {0: [], 2: []}
        for _ in range(2):
            t += SLOT
            self.stake([n0, n2], t)
            kernels[0].append(self.kernel_of(n0, n0.getbestblockhash()))
            kernels[2].append(self.kernel_of(n2, n2.getbestblockhash()))
        assert_equal(len(set(kernels[2])), 2)
        t = start
        for _ in range(6):
            t += SLOT
            self.stake([n1], t)
        assert_equal(n1.getblockcount(), LAST_POW + 6)
        assert_equal(n0.getblockcount(), LAST_POW + 2)
        assert_equal(n2.getblockcount(), LAST_POW + 2)

        self.log.info("Both reorganize onto node1's chain: their stakes are disconnected")
        # without the staking threads: stakers that find a block in every slot never converge
        self.restart(0, [], t)
        self.restart(1, [], t)
        self.restart(2, OLD, t)
        self.connect_all()
        sync_blocks(self.nodes)
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())
        assert_equal(n2.getbestblockhash(), n1.getbestblockhash())
        for i in (0, 2):
            for txid, vout in kernels[i]:
                assert self.nodes[i].gettxout(txid, vout) is not None

        self.log.info("node0 gave the coins back with their time: same chain state as node1")
        h0, h1, h2 = n0.getstatehashes(), n1.getstatehashes(), n2.getstatehashes()
        assert_equal(h0["utxo"], h1["utxo"])
        assert_equal(h0["utxo_legacy"], h1["utxo_legacy"])
        check = n0.repaircoins(True)
        assert_equal(check["damaged"], 0)
        assert_equal(check["scanned"], h0["coins"])
        assert_equal(n1.repaircoins(True)["damaged"], 0)

        self.log.info("node2 (-restorecointime=0) lost them, as versions before 4.0 did")
        assert_equal(h2["utxo_legacy"], h1["utxo_legacy"])
        assert h2["utxo"] != h1["utxo"]
        check = n2.repaircoins(True)
        assert check["damaged"] >= 2, check
        assert_equal(check["repaired"], check["damaged"])
        assert_equal(check["unreadable"], 0)
        # checkonly changes nothing
        assert_equal(n2.getstatehashes()["utxo"], h2["utxo"])

        self.log.info("node2 stakes a damaged coin with the time of its transaction; the others accept the block")
        self.restart(2, STAKING + OLD, t)
        self.connect_all()
        height = n2.getblockcount()
        t += SLOT
        for node in self.nodes:
            node.setmocktime(t)
        wait_until(lambda: n2.getblockcount() > height, timeout=60)
        sync_blocks(self.nodes)
        tip = n2.getbestblockhash()
        assert_equal(n0.getbestblockhash(), tip)
        assert_equal(n1.getbestblockhash(), tip)
        staked = self.kernel_of(n1, tip)
        assert staked in kernels[2], (staked, kernels[2])
        assert "Stake: coin %s:%d has no time in the chain state" % staked in self.log_of(2)
        assert "bad-cs-proofhash" not in self.log_of(0) + self.log_of(1) + self.log_of(2)

        self.log.info("repaircoins repairs what is left")
        self.restart(2, OLD, t)
        self.connect_all()
        before = n2.repaircoins(True)
        assert before["damaged"] >= 1, before
        result = n2.repaircoins()
        assert_equal(result["damaged"], before["damaged"])
        assert_equal(result["repaired"], before["damaged"])
        assert_equal(result["unreadable"], 0)
        assert_equal(n2.repaircoins(True)["damaged"], 0)
        sync_blocks(self.nodes)
        assert_equal(n2.getstatehashes()["utxo"], n1.getstatehashes()["utxo"])
        assert_equal(n0.getstatehashes()["utxo"], n1.getstatehashes()["utxo"])

        self.log.info("The repaired chain state survives a restart")
        self.restart(2, [], t)
        assert_equal(n2.repaircoins(True)["damaged"], 0)
        assert_equal(n2.getstatehashes()["utxo"], n1.getstatehashes()["utxo"])


if __name__ == '__main__':
    StakeCoinTimeTest().main()
