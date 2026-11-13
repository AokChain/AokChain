#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Copyright (c) 2017-2018 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Wallet encryption"""

import time

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class WalletEncryptionTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        passphrase = "WalletPassphrase"
        passphrase2 = "SecondWalletPassphrase"

        # Make sure the wallet isn't encrypted first
        address = self.nodes[0].getnewaddress()
        privkey = self.nodes[0].dumpprivkey(address)
        assert_equal(privkey[:1], "c")
        assert_equal(len(privkey), 52)

        # Encrypt the wallet
        self.nodes[0].node_encrypt_wallet(passphrase)
        self.start_node(0)

        # Test that the wallet is encrypted
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase with walletpassphrase first", self.nodes[0].dumpprivkey, address)

        # Check that walletpassphrase works
        self.nodes[0].walletpassphrase(passphrase, 2)
        assert_equal(privkey, self.nodes[0].dumpprivkey(address))

        # Check that the timeout is right
        time.sleep(2)
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase with walletpassphrase first", self.nodes[0].dumpprivkey, address)

        # Test wrong passphrase
        assert_raises_rpc_error(-14, "wallet passphrase entered was incorrect", self.nodes[0].walletpassphrase, passphrase + "wrong", 10)

        # Test walletlock
        self.nodes[0].walletpassphrase(passphrase, 84600)
        assert_equal(privkey, self.nodes[0].dumpprivkey(address))
        self.nodes[0].walletlock()
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase with walletpassphrase first", self.nodes[0].dumpprivkey, address)

        # Test passphrase changes
        self.nodes[0].walletpassphrasechange(passphrase, passphrase2)
        assert_raises_rpc_error(-14, "wallet passphrase entered was incorrect", self.nodes[0].walletpassphrase, passphrase, 10)
        self.nodes[0].walletpassphrase(passphrase2, 10)
        assert_equal(privkey, self.nodes[0].dumpprivkey(address))

        # Open for staking only, without a time limit: the keys sign stakes and nothing else
        self.nodes[0].walletlock()
        assert_raises_rpc_error(-8, "Timeout cannot be negative", self.nodes[0].walletpassphrase, passphrase2, -1)
        self.nodes[0].walletpassphrase(passphrase2, 0, True)
        time.sleep(2)
        assert_equal(self.nodes[0].getwalletinfo()["unlocked_until"], 0)
        assert_raises_rpc_error(-13, "unlocked for staking only", self.nodes[0].dumpprivkey, address)
        assert_raises_rpc_error(-13, "unlocked for staking only", self.nodes[0].sendtoaddress, address, 1)
        # a timed unlock for a payment; when its time is over the wallet is open for staking only
        # again, not locked: staking goes on
        self.nodes[0].walletpassphrase(passphrase2, 2)
        assert_equal(privkey, self.nodes[0].dumpprivkey(address))
        time.sleep(3)
        assert_raises_rpc_error(-13, "unlocked for staking only", self.nodes[0].dumpprivkey, address)
        self.nodes[0].walletlock()
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", self.nodes[0].dumpprivkey, address)
        # without stakingonly a timeout of 0 still locks at once
        self.nodes[0].walletpassphrase(passphrase2, 0)
        time.sleep(1)
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", self.nodes[0].dumpprivkey, address)

if __name__ == '__main__':
    WalletEncryptionTest().main()
