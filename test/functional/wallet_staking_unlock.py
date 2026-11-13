#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the staking-only unlock of encrypted wallets (walletpassphrase "pass" 0 true).

 - a walletpassphrase call that fails because of an argument leaves the wallet locked,
   also with the right passphrase (it used to stay open for payments, without a time)
 - two wallets: wallet A open for staking only without a time limit, then opened for
   payments for a few seconds; wallet B opened for payments for one second. When
   B's time is over B is locked, and when A's time is over A is open for staking only
   again. (The mark "back to staking only" was one for all wallets: B used up the mark
   of A, B stayed open for staking and A was locked.)
"""

import time

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

LOCKED = "Please enter the wallet passphrase"
STAKING_ONLY = "staking only"


class WalletStakingUnlockTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-keypool=2"]]

    def assert_staking_only(self, wallet, address):
        try:
            wallet.signmessage(address, "probe")
        except JSONRPCException as e:
            assert STAKING_ONLY in e.error["message"], e.error
            return
        raise AssertionError("the wallet signed a message while it was open for staking only")

    def run_test(self):
        node = self.nodes[0]
        address_a = node.getnewaddress()
        node.generate(30)
        node.sendtoaddress(address_a, 10)
        node.generate(1)
        coin = node.listunspent(1, 9999999, [address_a])[0]
        payment = node.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {address_a: 9})
        node.node_encrypt_wallet("passphrase-a")
        self.start_node(0)
        node = self.nodes[0]

        self.log.info("A call that fails because of an argument leaves the wallet locked")
        for args in ((-1, True), (-1,), ("soon",), (10, "yes")):
            assert_raises_rpc_error(None, None, node.walletpassphrase, "passphrase-a", *args)
            assert_equal(node.getwalletinfo()["unlocked_until"], 0)
            assert_raises_rpc_error(-13, LOCKED, node.signrawtransactionwithwallet, payment)

        self.log.info("Two wallets: each goes back to its own state when its time is over")
        node.createwallet("b")
        wallet_b = node.get_wallet_rpc("b")
        address_b = wallet_b.getnewaddress()
        wallet_b.encryptwallet("passphrase-b")
        node.wait_until_stopped()
        self.start_node(0, ["-wallet=wallet.dat", "-wallet=b"])
        node = self.nodes[0]
        wallet_a, wallet_b = node.get_wallet_rpc("wallet.dat"), node.get_wallet_rpc("b")
        wallet_a.walletpassphrase("passphrase-a", 0, True)
        self.assert_staking_only(wallet_a, address_a)
        wallet_a.walletpassphrase("passphrase-a", 6)
        wallet_a.signmessage(address_a, "probe")  # open for payments now
        wallet_b.walletpassphrase("passphrase-b", 1)
        time.sleep(2.5)
        # B's time is over: locked, not open for staking
        assert_raises_rpc_error(-13, LOCKED, wallet_b.signmessage, address_b, "probe")
        assert_equal(wallet_b.getwalletinfo()["unlocked_until"], 0)
        # A is still open for payments
        wallet_a.signmessage(address_a, "probe")
        time.sleep(5)
        # A's time is over: back to staking only, without a time limit
        self.assert_staking_only(wallet_a, address_a)
        wallet_a.walletlock()
        assert_raises_rpc_error(-13, LOCKED, wallet_a.signmessage, address_a, "probe")


if __name__ == '__main__':
    WalletStakingUnlockTest().main()
