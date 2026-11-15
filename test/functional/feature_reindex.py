#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2018 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test running aokchaind with -reindex and -reindex-chainstate options.

- Start a single node and generate 3 blocks.
- Stop the node and restart it with -reindex. Verify that the node has reindexed up to block 3.
- Stop the node and restart it with -reindex-chainstate. Verify that the node has reindexed up to block 3.
- Turn an index on or off: the node does not start and says that -reindex is needed;
  -reindex-chainstate does not change the index, -reindex does.
"""

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal
import time

class ReindexTest(AokChainTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def reindex(self, justchainstate=False):
        self.nodes[0].generate(3)
        blockcount = self.nodes[0].getblockcount()
        self.stop_nodes()
        extra_args = [["-reindex-chainstate" if justchainstate else "-reindex", "-checkblockindex=1"]]
        self.start_nodes(extra_args)
        while self.nodes[0].getblockcount() < blockcount:
            time.sleep(0.1)
        assert_equal(self.nodes[0].getblockcount(), blockcount)
        self.log.info("Success")

    def change_index(self):
        blockcount = self.nodes[0].getblockcount()
        self.stop_nodes()
        # (-txindex and -tokenindex are on by default)
        for option, value in (("txindex", 0), ("tokenindex", 0), ("addressindex", 1), ("spentindex", 1), ("timestampindex", 1)):
            self.log.info("Changing -%s needs -reindex, and the error says so" % option)
            needed = "You need to rebuild the database using -reindex to change -%s.\nPlease restart with -reindex to recover." % option
            self.assert_start_raises_init_error(0, ["-%s=%d" % (option, value)], needed)
            self.assert_start_raises_init_error(0, ["-%s=%d" % (option, value), "-reindex-chainstate"], needed)
        self.start_node(0, ["-addressindex=1", "-reindex"])
        while self.nodes[0].getblockcount() < blockcount:
            time.sleep(0.1)
        self.nodes[0].generate(1)
        # (without the index this call fails)
        assert_equal(self.nodes[0].getaddressbalance({"addresses": [self.nodes[0].getnewaddress()]})["balance"], 0)
        self.stop_nodes()
        self.start_node(0, ["-addressindex=1"])  # kept now
        assert_equal(self.nodes[0].getblockcount(), blockcount + 1)

    def run_test(self):
        self.reindex(False)
        self.reindex(True)
        self.reindex(False)
        self.reindex(True)
        self.change_index()

if __name__ == '__main__':
    ReindexTest().main()
