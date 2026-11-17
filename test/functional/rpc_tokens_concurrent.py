#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the token read commands from several clients at once.

The node keeps the metadata of the most recently used tokens in a cache of
2500 entries. With more tokens than that, looking tokens up one after another
replaces the entries of the cache all the way through. Here several clients
list tokens, look single tokens up and ask for an address's token balances
while blocks with token transfers are connected. The node has to stay up, and
every answer has to be complete and the same as before.
"""

import threading
import time

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, get_rpc_proxy

ROOTS = 6
UNIQUE_PER_ROOT = 500   # 6 * 500 unique tokens: more than the metadata cache holds
RUN_SECONDS = 20


class Reader(threading.Thread):
    """Repeats one read on a connection of its own until told to stop."""

    def __init__(self, node, work):
        super().__init__(daemon=True)
        self.rpc = get_rpc_proxy(node.url, 1, timeout=120, coveragedir=node.coverage_dir)
        self.work = work
        self.stop = threading.Event()
        self.calls = 0
        self.error = None

    def run(self):
        try:
            while not self.stop.is_set():
                self.work(self.rpc)
                self.calls += 1
        except Exception as e:  # pylint: disable=broad-except
            self.error = e


class TokenReadsTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-addressindex=1", "-rpcthreads=8", "-rpcworkqueue=64"]]

    def run_test(self):
        node = self.nodes[0]
        node.generate(20)
        holder = node.getnewaddress()
        other = node.getnewaddress()

        self.log.info("Issue tokens with different units and more unique tokens than the metadata cache holds")
        roots = ["READS%d" % i for i in range(ROOTS)]
        for units, root in enumerate(roots):
            node.issue(root, 1000, holder, "", units, True)
        node.sendtoaddress(holder, 5)
        node.generate(1)
        for root in roots:
            node.issueunique(root, ["N%d" % k for k in range(UNIQUE_PER_ROOT)], None, holder)
            node.generate(1)
        total = ROOTS + ROOTS * UNIQUE_PER_ROOT
        assert_equal(len(node.listtokens("", False)), total)
        owners = [root + "!" for root in roots]

        self.log.info("A verbose listing gives every amount in the token's own units")
        listing = node.listtokens("", True)
        assert_equal(len(listing), total)
        for units, root in enumerate(roots):
            data = node.gettokendata(root)
            assert_equal(listing[root]["units"], units)
            assert_equal(listing[root]["amount"], data["amount"])
            assert_equal(listing[root]["amount"], 1000)
            assert_equal(listing[root + "#N0"]["amount"], 1)
            assert_equal(listing[root + "#N0"]["units"], 0)
        for owner in owners:
            assert_equal(node.gettokendata(owner), None)
        balances = {b["tokenName"]: b for b in node.getaddressbalance({"addresses": [holder]}, True)}
        for units, root in enumerate(roots):
            assert_equal(balances[root]["units"], units)
            assert_equal(balances[root]["received"], 1000 * 10 ** 8)
        assert_equal(balances["AOK"]["units"], 8)

        self.log.info("Readers on several connections while blocks with token transfers are connected")

        def list_all(rpc):
            assert_equal(len(rpc.listtokens("", True)), total)

        def list_page(rpc):
            assert_equal(len(rpc.listtokens("*", True, 200, 0)), 200)

        def look_up(rpc):
            for owner in owners:
                assert_equal(rpc.gettokendata(owner), None)
            for units, root in enumerate(roots):
                assert_equal(rpc.gettokendata(root)["units"], units)

        def address_balance(rpc):
            got = {b["tokenName"]: b["units"] for b in rpc.getaddressbalance({"addresses": [holder]}, True)}
            for units, root in enumerate(roots):
                assert_equal(got[root], units)

        readers = [Reader(node, work) for work in
                   [list_all, list_all, list_all, list_page, list_page, look_up, look_up, address_balance]]
        for reader in readers:
            reader.start()
        end = time.time() + RUN_SECONDS
        sent = 0
        while time.time() < end:
            node.transfer(roots[sent % ROOTS], 1, other)
            node.generate(1)
            sent += 1
            assert not any(reader.error for reader in readers), [repr(reader.error) for reader in readers if reader.error]
            time.sleep(0.3)
        for reader in readers:
            reader.stop.set()
        for reader in readers:
            reader.join(timeout=120)
        for reader in readers:
            assert reader.error is None, repr(reader.error)
            assert reader.calls > 0
        self.log.info("%d blocks connected, %d reads answered" % (sent, sum(reader.calls for reader in readers)))

        self.log.info("The node is up and the answers are what they were")
        assert_equal(len(node.listtokens("", True)), total)
        after = node.listtokens("", True)
        for units, root in enumerate(roots):
            assert_equal(after[root]["amount"], 1000)
            assert_equal(after[root]["units"], units)

        self.log.info("A reissue that changes the units shows in the listing")
        node.reissue(roots[0], 50, holder, "", True, 6)
        node.generate(1)
        after = node.listtokens(roots[0], True)
        assert_equal(after[roots[0]]["units"], 6)
        assert_equal(after[roots[0]]["amount"], 1050)
        assert_equal(node.gettokendata(roots[0])["amount"], 1050)


if __name__ == '__main__':
    TokenReadsTest().main()
