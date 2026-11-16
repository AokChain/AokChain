#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2018 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test multiwallet.

Verify that a aokchaind node can load multiple wallet files:

 - -wallet options that cannot work stop the start, with the reason
 - every wallet has its own endpoint, coins and transactions
 - loadwallet, createwallet and restorewallet refuse what cannot be done with
   the error codes that callers know
"""
import os
import shutil

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class MultiWalletTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-wallet=w1', '-wallet=w2', '-wallet=w3']]

    def wallet_dir(self, *parts):
        base = os.path.join(self.options.tmpdir, 'node0', 'regtest')
        if os.path.isdir(os.path.join(base, 'wallets')):
            base = os.path.join(base, 'wallets')
        return os.path.join(base, *parts)

    def run_test(self):
        node = self.nodes[0]
        assert_equal(set(node.listwallets()), {"w1", "w2", "w3"})
        # a wallet named by -wallet is a folder with its wallet.dat
        for name in ("w1", "w2", "w3"):
            assert os.path.isfile(self.wallet_dir(name, "wallet.dat"))

        self.stop_node(0)

        self.log.info("-wallet options that stop the start")
        # the same wallet twice
        self.assert_start_raises_init_error(0, ['-wallet=w1', '-wallet=w1'], 'Error loading wallet w1. Duplicate -wallet filename specified.')
        # a wallet that is a copy of another one
        os.mkdir(self.wallet_dir('w22'))
        shutil.copyfile(self.wallet_dir('w2', 'wallet.dat'), self.wallet_dir('w22', 'wallet.dat'))
        self.assert_start_raises_init_error(0, ['-wallet=w2', '-wallet=w22'], 'duplicates fileid')
        # a link to a wallet file, and a file inside a folder: a wallet is named by its folder
        os.symlink(self.wallet_dir('w1', 'wallet.dat'), self.wallet_dir('w12'))
        self.assert_start_raises_init_error(0, ['-wallet=w12'], "Invalid -wallet path 'w12'")
        self.assert_start_raises_init_error(0, ['-wallet=w1/wallet.dat'], "Invalid -wallet path 'w1/wallet.dat'")
        # a wallet directory that does not exist
        self.assert_start_raises_init_error(0, ['-walletdir=' + self.wallet_dir('nowhere')], 'does not exist')

        self.start_node(0, self.extra_args[0])
        node = self.nodes[0]

        w1 = node.get_wallet_rpc("w1")
        w2 = node.get_wallet_rpc("w2")
        w3 = node.get_wallet_rpc("w3")
        wallet_bad = node.get_wallet_rpc("bad")

        self.log.info("Every wallet has its endpoint")
        # accessing invalid wallet fails
        assert_raises_rpc_error(-18, "Requested wallet does not exist or is not loaded", wallet_bad.getwalletinfo)
        # accessing wallet RPC without using wallet endpoint fails
        assert_raises_rpc_error(-19, "Wallet file not specified", node.getwalletinfo)
        assert_equal(w1.getwalletinfo()['walletname'], "w1")
        assert_equal(w2.getwalletinfo()['walletname'], "w2")
        assert_equal(w3.getwalletinfo()['walletname'], "w3")

        self.log.info("Every wallet has its own coins and transactions")
        w1.generate(40)
        assert w1.getbalance() > 3
        assert_equal(w2.getbalance(), 0)
        assert_equal(w3.getbalance(), 0)
        to_w2 = w1.sendtoaddress(w2.getnewaddress(), 1)
        to_w3 = w1.sendtoaddress(w3.getnewaddress(), 2)
        w1.generate(1)
        assert_equal(w2.getbalance(), 1)
        assert_equal(w3.getbalance(), 2)
        assert_equal(w2.gettransaction(to_w2)["amount"], 1)
        assert_raises_rpc_error(-5, "Invalid or non-wallet transaction id", w2.gettransaction, to_w3)
        assert_raises_rpc_error(-5, "Invalid or non-wallet transaction id", w3.gettransaction, to_w2)
        assert_equal(len(w2.listtransactions()), 1)

        batch = w1.batch([w1.getblockchaininfo.get_request(), w1.getwalletinfo.get_request()])
        assert_equal(batch[0]["result"]["chain"], "regtest")
        assert_equal(batch[1]["result"]["walletname"], "w1")

        self.log.info("What loadwallet, createwallet and restorewallet refuse")
        listed = {entry["name"] for entry in node.listwalletdir()["wallets"]}
        assert {"w1", "w2", "w3"} <= listed, listed
        assert_raises_rpc_error(-18, "Wallet nothing not found", node.loadwallet, "nothing")
        os.mkdir(self.wallet_dir("empty"))
        assert_raises_rpc_error(-18, "does not contain a wallet.dat file", node.loadwallet, "empty")
        assert_raises_rpc_error(-4, "Wallet w1 is already loaded", node.loadwallet, "w1")
        assert_raises_rpc_error(-4, "Wallet w1 already exists", node.createwallet, "w1")
        assert_raises_rpc_error(-1, "duplicates fileid", node.loadwallet, "w22")
        for name in ("../outside", "w1/../../outside", "."):
            assert_raises_rpc_error(-8, "Invalid wallet name", node.createwallet, name)
            assert_raises_rpc_error(-8, "Invalid wallet name", node.loadwallet, name)
        backup = os.path.join(self.options.tmpdir, "w2-backup.dat")
        w2.backupwallet(backup)
        assert_raises_rpc_error(-8, "Backup file does not exist", node.restorewallet, "w9", backup + ".missing")
        assert_raises_rpc_error(-8, "plain name without a path", node.restorewallet, "a/b", backup)
        assert_raises_rpc_error(-4, "Wallet w1 is already loaded", node.restorewallet, "w1", backup)
        assert_raises_rpc_error(-4, "of that name already", node.restorewallet, "empty", backup)
        # nothing was loaded or left behind by the refused calls
        assert_equal(set(node.listwallets()), {"w1", "w2", "w3"})
        assert not os.path.exists(self.wallet_dir("w9"))
        assert not os.path.exists(os.path.join(os.path.dirname(self.wallet_dir()), "outside"))

        self.log.info("A fourth wallet is created, used and found again after a restart")
        assert_equal(node.createwallet("w4")["name"], "w4")
        w4 = node.get_wallet_rpc("w4")
        w1.sendtoaddress(w4.getnewaddress(), 4)
        w1.generate(1)
        assert_equal(w4.getbalance(), 4)
        self.stop_node(0)
        self.start_node(0, ['-wallet=w1', '-wallet=w4'])
        node = self.nodes[0]
        assert_equal(node.listwallets(), ["w1", "w4"])
        assert_equal(node.get_wallet_rpc("w4").getbalance(), 4)

if __name__ == '__main__':
    MultiWalletTest().main()
