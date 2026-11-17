#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test staking with a wallet coin that the chain state of the node does not hold.

A node whose chain state lacks an output that the chain has (a damaged chain
state) cannot stake that coin. Before, the wallet still added it to stakes of
its address as a further input: every such stake was invalid for the node
itself, and the wallet staked nothing at all.

 - the other coins of the address stake; the missing one is left out
 - getstakinginfo counts it ("coins_missing") once it has stayed away over
   several blocks, and debug.log names it
 - a rebuild of the chain state (-reindex-chainstate) brings it back
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, wait_until

LAST_POW = 100
SLOT = 16
THRESHOLD = 50


class StakeMissingCoinTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.base = ["-lastpowblock=%d" % LAST_POW, "-stakecombinethreshold=%d" % THRESHOLD]
        self.staking = self.base + ["-staking=1", "-emergencystaking=1"]
        self.extra_args = [self.base + ["-staking=0"], self.staking]

    def step(self, node, blocks=1):
        """Advance the clock slot by slot until node's chain has grown by the number of blocks."""
        for _ in range(blocks):
            height = node.getblockcount()
            self.t += SLOT
            for n in self.nodes:
                n.setmocktime(self.t)
            wait_until(lambda: node.getblockcount() > height, timeout=60)
            sync_blocks(self.nodes)

    def run_test(self):
        funder, staker = self.nodes
        self.t = funder.getblock(funder.getbestblockhash())["time"] + 600
        self.t -= self.t % SLOT
        for node in self.nodes:
            node.setmocktime(self.t)

        self.log.info("The staker gets six coins of 10 at one address and three of 100 at addresses of their own")
        funder.generate(30)
        address = staker.getnewaddress()
        small = [funder.sendtoaddress(address, 10) for _ in range(6)]
        for _ in range(3):
            funder.sendtoaddress(staker.getnewaddress(), 100)
        funder.generate(LAST_POW - 30)
        sync_blocks(self.nodes)
        assert_equal(staker.getbalance(), Decimal("360"))
        assert_equal(staker.getstakinginfo()["coins_missing"], 0)

        self.log.info("One of the small coins disappears from the chain state of the staker's node")
        lost = [u for u in staker.listunspent() if u["txid"] == small[2]][0]
        assert_equal(staker.removecoin(lost["txid"], lost["vout"]), True)
        assert_equal(staker.gettxout(lost["txid"], lost["vout"]), None)
        assert funder.gettxout(lost["txid"], lost["vout"]) is not None
        # (the wallet still has it)
        assert_equal(len([u for u in staker.listunspent() if u["txid"] == lost["txid"]]), 1)

        self.log.info("The staker stakes all its other coins: four blocks, one of them with the five small coins that are left")
        self.t = funder.getblock(funder.getbestblockhash())["time"] + 2 * SLOT
        self.t -= self.t % SLOT
        start = staker.getblockcount()
        self.step(staker, 4)
        assert_equal(funder.getblockcount(), start + 4)
        combined = 0
        for height in range(start + 1, start + 5):
            block = staker.getblock(staker.getblockhash(height), 2)
            assert "proof-of-stake" in block["flags"]
            inputs = [(vin["txid"], vin["vout"]) for vin in block["tx"][1]["vin"]]
            assert (lost["txid"], lost["vout"]) not in inputs, inputs
            if len(inputs) > 1:
                combined += 1
                assert_equal(sorted(txid for txid, _ in inputs), sorted(txid for txid in small if txid != lost["txid"]))
        assert_equal(combined, 1)

        self.log.info("The coin is reported once it has stayed away over several blocks")
        # (the funder stakes now; the staker has nothing left but the missing coin)
        self.stop_node(0)
        self.start_node(0, self.staking + ["-mocktime=%d" % self.t])
        connect_nodes_bi(self.nodes, 0, 1)
        funder = self.nodes[0]
        self.step(funder, 4)
        wait_until(lambda: staker.getstakinginfo()["coins_missing"] == 1, timeout=30)
        with open(self.nodes[1].datadir + "/regtest/debug.log", encoding="utf8") as log:
            text = log.read()
        assert "Staking: COutPoint(%s, %d) is unspent for wallet" % (lost["txid"][:10], lost["vout"]) in text
        assert_equal(text.count("is unspent for wallet"), 1)

        self.log.info("A rebuild of the chain state brings the coin back")
        self.stop_node(1)
        self.start_node(1, self.staking + ["-mocktime=%d" % self.t, "-reindex-chainstate"])
        connect_nodes_bi(self.nodes, 0, 1)
        staker = self.nodes[1]
        sync_blocks(self.nodes)
        assert staker.gettxout(lost["txid"], lost["vout"]) is not None
        self.step(funder, 1)
        assert_equal(staker.getstakinginfo()["coins_missing"], 0)
        assert_equal(staker.getstatehashes()["utxo"], funder.getstatehashes()["utxo"])


if __name__ == '__main__':
    StakeMissingCoinTest().main()
