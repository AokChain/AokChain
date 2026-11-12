#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test unloadwallet.

 - a wallet that was created or loaded while the node runs can be unloaded and
   loaded again; what happened to its addresses meanwhile is found on loading
 - the first wallet cannot be unloaded
 - with one wallet left, wallet calls need no endpoint again
 - the node stops and starts cleanly afterwards
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletUnloadTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        node.generate(20)
        first = node.listwallets()[0]

        self.log.info("A second wallet is created and funded")
        assert_equal(node.createwallet("second")["name"], "second")
        assert_equal(node.listwallets(), [first, "second"])
        w1 = node.get_wallet_rpc(first)
        w2 = node.get_wallet_rpc("second")
        address = w2.getnewaddress()
        w1.sendtoaddress(address, 10)
        w1.generate(1)
        assert_equal(w2.getbalance(), Decimal("10"))
        # with two wallets a call must say which one it means
        assert_raises_rpc_error(-19, "Wallet file not specified", node.getbalance)

        self.log.info("What cannot be unloaded")
        assert_raises_rpc_error(-1, "The first wallet cannot be unloaded", node.unloadwallet, first)
        assert_raises_rpc_error(-1, "The first wallet cannot be unloaded", w1.unloadwallet)
        assert_raises_rpc_error(-18, "does not exist or is not loaded", node.unloadwallet, "nothing")
        assert_raises_rpc_error(-8, "Give the name of the wallet", node.unloadwallet)
        assert_raises_rpc_error(-4, "is already loaded", node.loadwallet, "second")
        assert_equal(node.listwallets(), [first, "second"])

        self.log.info("The second wallet is unloaded; the first one is the only one again")
        result = node.unloadwallet("second")
        assert_equal(result["name"], "second")
        assert_equal(result["warning"], "")
        assert_equal(node.listwallets(), [first])
        assert_raises_rpc_error(-18, "does not exist or is not loaded", w2.getbalance)
        assert_raises_rpc_error(-18, "does not exist or is not loaded", node.unloadwallet, "second")
        balance = node.getbalance()
        node.sendtoaddress(address, 5)
        node.generate(1)
        assert node.getbalance() < balance

        self.log.info("Loaded again it finds what it received meanwhile")
        assert_equal(node.loadwallet("second")["name"], "second")
        assert_equal(node.listwallets(), [first, "second"])
        assert_equal(w2.getbalance(), Decimal("15"))
        w2.sendtoaddress(w1.getnewaddress(), 3)
        w1.generate(1)
        assert w2.getbalance() < Decimal("12")

        self.log.info("Unloaded through its endpoint; several times in a row")
        for _ in range(3):
            assert_equal(w2.unloadwallet()["warning"], "")
            assert_equal(node.listwallets(), [first])
            node.generate(1)
            node.loadwallet("second")
            assert w2.getbalance() < Decimal("12")
        node.unloadwallet("second")

        self.log.info("The node stops and starts cleanly")
        height = node.getblockcount()
        self.stop_node(0)
        self.start_node(0)
        assert_equal(node.listwallets(), [first])
        assert_equal(node.getblockcount(), height)
        node.loadwallet("second")
        assert node.get_wallet_rpc("second").getbalance() < Decimal("12")
        self.stop_node(0)
        self.start_node(0, ["-wallet=" + first, "-wallet=second"])
        assert_equal(node.listwallets(), [first, "second"])
        node.unloadwallet("second")
        assert_equal(node.listwallets(), [first])


if __name__ == '__main__':
    WalletUnloadTest().main()
