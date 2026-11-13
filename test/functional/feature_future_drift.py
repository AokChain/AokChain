#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the smaller allowance for the time of a proof-of-stake block.

From a height that is not scheduled on the main network (-futuredriftheight on
regtest) a proof-of-stake block may be at most 60 seconds ahead of the clock
of the node that receives it, instead of 10 minutes.
"""

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, wait_until

LAST_POW = 100
SLOT = 16


class FutureDriftTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        base = ["-lastpowblock=%d" % LAST_POW]
        # node1 has the rule from the first proof-of-stake block, node2 does not have it
        self.extra_args = [base + ["-staking=1", "-emergencystaking=1"], base + ["-futuredriftheight=0"], base]

    def log_of(self, i):
        import os
        with open(os.path.join(self.nodes[i].datadir, "regtest", "debug.log"), errors="replace") as f:
            return f.read()

    def run_test(self):
        staker, strict, old = self.nodes
        connect_nodes_bi(self.nodes, 0, 2)  # (the old node hears the staker itself, not through the strict one)
        t = staker.getblock(staker.getbestblockhash())["time"] + 600
        t -= t % SLOT
        for node in self.nodes:
            node.setmocktime(t)
        # the proof-of-work blocks are no concern of the rule, however fast they come
        staker.generate(LAST_POW)
        sync_blocks(self.nodes)

        t = staker.getblock(staker.getbestblockhash())["time"] + 2 * SLOT
        t -= t % SLOT
        self.log.info("A stake 32 seconds ahead of the receiver's clock is accepted by both")
        t += 2 * SLOT
        strict.setmocktime(t - 32)
        old.setmocktime(t - 32)
        staker.setmocktime(t)
        wait_until(lambda: staker.getblockcount() == LAST_POW + 1, timeout=60)
        sync_blocks(self.nodes)

        self.log.info("A stake 5 minutes ahead: accepted under the old rule, refused under the new one")
        t += 20 * SLOT
        staker.setmocktime(t)
        wait_until(lambda: staker.getblockcount() == LAST_POW + 2, timeout=60)
        sync_blocks([staker, old])
        assert_equal(strict.getblockcount(), LAST_POW + 1)
        wait_until(lambda: "time-too-new" in self.log_of(1), timeout=30)

        self.log.info("When its clock has caught up, the strict node takes the block")
        strict.setmocktime(t)
        old.setmocktime(t)
        self.stop_node(1)
        self.start_node(1, ["-lastpowblock=%d" % LAST_POW, "-futuredriftheight=0", "-mocktime=%d" % t])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes)
        assert_equal(strict.getbestblockhash(), staker.getbestblockhash())


if __name__ == '__main__':
    FutureDriftTest().main()
