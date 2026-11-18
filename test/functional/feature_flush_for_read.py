#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test what a command that reads the databases costs.

The token listing commands and gettxoutsetinfo read the databases themselves, so they first
ask the node to write what it has in memory.
 - When the databases hold everything already, nothing is written: not the chain state, not
   the token database, and nothing is read for the note of what the governance and the token
   database hold.
 - After a block, the first such command writes; the note is not computed for it (that reads
   both databases). There is then no note for that block: a start after a kill has nothing to
   check, and finds nothing wrong.
 - A write that no command asked for (here: a normal stop) brings the note up to date, and the
   next start checks the two databases against it.
"""

import os

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal

COINS = "to coin database..."
TOKENS = "Token database: "
NOTE = "Note of the governance and the token database for block"
CHECKED = "are what they were when the chain state was written"
DAMAGED = "A database of this node is damaged"


class FlushForReadTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-debug=coindb"]]

    def counts(self):
        with open(os.path.join(self.nodes[0].datadir, "regtest", "debug.log"), encoding="utf8") as log:
            text = log.read()
        return {name: text.count(name) for name in (COINS, TOKENS, NOTE, CHECKED, DAMAGED)}

    def readers(self, node, address):
        node.listtokens()
        node.listtokens("FLUSH*", True)
        node.listtokenbalancesbyaddress(address)
        node.listaddressesbytoken("FLUSHTOKEN")
        node.gettxoutsetinfo()

    def run_test(self):
        node = self.nodes[0]
        node.generate(110)
        address = node.getnewaddress()
        node.issue("FLUSHTOKEN", 1000, address)
        node.generate(1)

        self.log.info("The first command after a block writes; the commands after it write nothing")
        before = self.counts()
        assert_equal(len(node.listtokens()), 1)
        first = self.counts()
        assert_equal(first[COINS], before[COINS] + 1)
        assert_equal(first[TOKENS], before[TOKENS] + 1)
        for _ in range(5):
            self.readers(node, address)
        assert_equal(self.counts(), first)

        self.log.info("After a transfer: one write, and no read of the databases for the note")
        node.transfer("FLUSHTOKEN", 10, node.getnewaddress())
        node.generate(1)
        balances = node.listtokenbalancesbyaddress(address)
        after = self.counts()
        assert_equal(after[COINS], first[COINS] + 1)
        assert_equal(after[TOKENS], first[TOKENS] + 1)
        assert_equal(after[NOTE], first[NOTE])
        for _ in range(5):
            self.readers(node, address)
        assert_equal(self.counts(), after)

        self.log.info("A kill: the start after it has no note for that block, and nothing is wrong")
        node.process.kill()
        node.process.wait()
        node.running = False
        node.process = None
        node.rpc_connected = False
        node.rpc = None
        self.start_node(0, self.extra_args[0])
        node = self.nodes[0]
        started = self.counts()
        assert_equal(started[CHECKED], after[CHECKED])
        assert_equal(started[DAMAGED], 0)
        assert_equal(node.listtokenbalancesbyaddress(address), balances)
        assert_equal(node.getblockcount(), 112)
        # (a start writes its state once, and no command asked for that: the note is there again)
        assert_equal(started[NOTE], after[NOTE] + 1)

        self.log.info("A normal stop brings the note up to date; the next start checks the databases against it")
        node.transfer("FLUSHTOKEN", 5, node.getnewaddress())
        node.generate(1)
        balances = node.listtokenbalancesbyaddress(address)
        before_stop = self.counts()
        assert_equal(before_stop[NOTE], started[NOTE])
        self.stop_node(0)
        stopped = self.counts()
        assert_equal(stopped[NOTE], before_stop[NOTE] + 1)
        with open(os.path.join(node.datadir, "regtest", "debug.log"), encoding="utf8") as log:
            assert log.read().rstrip().endswith("Shutdown: done")
        self.start_node(0, self.extra_args[0])
        node = self.nodes[0]
        checked = self.counts()
        assert_equal(checked[CHECKED], stopped[CHECKED] + 1)
        assert_equal(checked[DAMAGED], 0)
        # the note that was checked is right: a stop that has nothing to write does not compute it again
        assert_equal(checked[NOTE], stopped[NOTE])
        self.stop_node(0)
        assert_equal(self.counts()[NOTE], stopped[NOTE])
        self.start_node(0, self.extra_args[0])
        node = self.nodes[0]
        assert_equal(self.counts()[CHECKED], checked[CHECKED] + 1)
        assert_equal(self.counts()[DAMAGED], 0)
        assert_equal(node.listtokenbalancesbyaddress(address), balances)
        # and the commands still write nothing
        quiet = self.counts()
        for _ in range(3):
            self.readers(node, address)
        assert_equal(self.counts(), quiet)


if __name__ == '__main__':
    FlushForReadTest().main()
