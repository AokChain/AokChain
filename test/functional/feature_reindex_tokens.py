#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test -reindex-chainstate and -reindex with tokens issued on chain.

The token and governance databases are derived from the connected blocks.
They used to survive -reindex-chainstate, so reconnecting the chain failed
at the first token issuance ("token_name ... has already been used") and the
node stopped at the block before it, marking a valid block invalid.

A start with -reindex-chainstate that stops because an index was turned on or
off (only -reindex changes an index) used to wipe the token database before it
stopped; the next start then failed to verify the blocks. It leaves the token
database as it is now.
"""

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal


class ReindexTokensTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def check_tokens(self, node, height, best):
        assert_equal(node.getblockcount(), height)
        assert_equal(node.getbestblockhash(), best)
        data = node.gettokendata("REINDEXTEST")
        assert_equal(data["name"], "REINDEXTEST")
        assert_equal(data["amount"], 1000)

    def run_test(self):
        node = self.nodes[0]
        node.generate(20)

        self.log.info("Issue a token")
        node.issue("REINDEXTEST", 1000)
        node.generate(5)
        height, best = node.getblockcount(), node.getbestblockhash()
        self.check_tokens(node, height, best)

        for option in ["-reindex-chainstate", "-reindex"]:
            self.log.info("Restart with %s" % option)
            self.stop_node(0)
            self.start_node(0, extra_args=[option])
            self.wait_until_reindexed(node, height)
            self.check_tokens(node, height, best)

        self.log.info("-reindex-chainstate with an index turned off stops, and the tokens stay")
        # (with a transfer among the blocks that the start verifies, the next start used to fail)
        node.transfer("REINDEXTEST", 10, node.getnewaddress())
        node.generate(1)
        height, best = node.getblockcount(), node.getbestblockhash()
        self.stop_node(0)
        self.start_node(0, extra_args=["-addressindex=1", "-reindex"])
        self.wait_until_reindexed(node, height)
        holders = node.listaddressesbytoken("REINDEXTEST")
        self.stop_node(0)
        self.assert_start_raises_init_error(0, ["-reindex-chainstate"],
                                            "You need to rebuild the database using -reindex to change -addressindex")
        self.start_node(0, extra_args=["-addressindex=1"])
        self.check_tokens(node, height, best)
        assert_equal(node.listaddressesbytoken("REINDEXTEST"), holders)

    def wait_until_reindexed(self, node, height):
        from test_framework.util import wait_until
        wait_until(lambda: node.getblockcount() == height, timeout=60)


if __name__ == '__main__':
    ReindexTokensTest().main()
