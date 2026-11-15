#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test removeaddress on a node that keeps the blocks and on a pruned node.

 - removeaddress stops watching an address that importaddress added
 - then it rescans the wallet, on a node that keeps the blocks
 - a pruned node does not have the blocks for a rescan: it removes the
   address without one (importaddress refuses its rescan there)
"""

import os

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class RemoveAddressTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        # node 2 prunes (manual pruning, which does not go with -txindex)
        self.extra_args = [[], [], ["-prune=1", "-txindex=0"]]

    def rescans(self, node):
        """How many times the wallet of the node has rescanned the chain."""
        with open(os.path.join(node.datadir, "regtest", "debug.log"), encoding="utf-8") as log:
            return log.read().count("RescanFromTime: Rescanning last")

    def run_test(self):
        miner, full, pruned = self.nodes
        miner.generate(30)
        watched = miner.getnewaddress()
        miner.sendtoaddress(watched, 5)
        miner.generate(1)
        self.sync_all()

        self.log.info("Both nodes watch an address of another wallet")
        assert_raises_rpc_error(-4, "Rescan is disabled in pruned mode", pruned.importaddress, watched, "watched", True)
        for node in (full, pruned):
            node.importaddress(watched, "watched", False)
            assert_equal(node.validateaddress(watched)["iswatchonly"], True)

        self.log.info("removeaddress stops watching it; it rescans unless the node prunes")
        before = {node.index: self.rescans(node) for node in (full, pruned)}
        for node in (full, pruned):
            node.removeaddress(watched)
            assert_equal(node.validateaddress(watched)["iswatchonly"], False)
        assert_equal(self.rescans(full), before[full.index] + 1)
        assert_equal(self.rescans(pruned), before[pruned.index])


if __name__ == '__main__':
    RemoveAddressTest().main()
