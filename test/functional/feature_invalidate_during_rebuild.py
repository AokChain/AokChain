#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test invalidateblock and reconsiderblock while the chain state is being built again.

A rebuild of the chain state connects the whole chain in one run of the loop that activates the
best chain. A block that is marked invalid while that loop is on its way, and that it has not
reached yet, must not be connected by it: the node stops at the block before it and goes on
running. When the mark is taken away again, the node goes on to the tip.

-testactivationpause (-regtest only) makes the rebuild slow enough for the command to come in
the middle of it.
"""

import time

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, wait_until

PAUSE = ["-reindex-chainstate", "-testactivationpause=20"]


class InvalidateDuringRebuildTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def status(self, node, block):
        return [tip["status"] for tip in node.getchaintips() if tip["hash"] == block]

    def run_test(self):
        node = self.nodes[0]
        node.generate(150)
        tip = node.getbestblockhash()
        marked, before = node.getblockhash(140), node.getblockhash(139)
        hashes = node.getstatehashes()

        self.log.info("A block is marked invalid while the rebuild has not reached it")
        self.stop_node(0)
        self.start_node(0, PAUSE)
        node = self.nodes[0]
        assert node.getblockcount() < 139
        node.invalidateblock(marked)
        wait_until(lambda: node.getblockcount() == 139, timeout=240)
        time.sleep(2)   # (it stays there, and the node runs)
        assert_equal(node.getbestblockhash(), before)
        assert_equal(node.getblockcount(), 139)

        self.log.info("The mark is taken away: the node goes on to the tip")
        node.reconsiderblock(marked)
        wait_until(lambda: node.getbestblockhash() == tip, timeout=240)
        for part in ("utxo", "tokens", "governance", "index"):
            assert_equal(node.getstatehashes()[part], hashes[part])

        self.log.info("The mark is taken away while a rebuild is on its way to the block before it")
        node.invalidateblock(marked)
        assert_equal(node.getbestblockhash(), before)
        self.stop_node(0)
        self.start_node(0, PAUSE)
        node = self.nodes[0]
        assert node.getblockcount() < 139
        node.reconsiderblock(marked)
        wait_until(lambda: node.getbestblockhash() == tip, timeout=240)
        for part in ("utxo", "tokens", "governance", "index"):
            assert_equal(node.getstatehashes()[part], hashes[part])

        self.log.info("A block that the rebuild has passed is marked invalid: the node goes back before it and stays")
        self.stop_node(0)
        self.start_node(0, PAUSE)
        node = self.nodes[0]
        early = node.getblockhash(3)
        wait_until(lambda: node.getblockcount() >= 10, timeout=240)
        node.invalidateblock(early)
        time.sleep(2)
        assert_equal(node.getblockcount(), 2)
        node.reconsiderblock(early)
        wait_until(lambda: node.getbestblockhash() == tip, timeout=240)
        for part in ("utxo", "tokens", "governance", "index"):
            assert_equal(node.getstatehashes()[part], hashes[part])


if __name__ == '__main__':
    InvalidateDuringRebuildTest().main()
