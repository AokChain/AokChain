#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test aokchain-wallet, the tool that works on a wallet file without a node.

 - info and addresses show what a wallet of a node contains
 - a wallet that a running node uses is refused
 - create makes a wallet that a node loads, with the same keys
 - salvage keeps the keys of a wallet
"""

import os
import shutil
import subprocess

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal


class ToolWalletTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def tool(self, *args, fail=False):
        binary = os.path.join(os.path.dirname(os.getenv("AOKCHAIND", "aokchaind")), "aokchain-wallet")
        if not os.path.dirname(binary):
            binary = "aokchain-wallet"
        result = subprocess.run([binary, "-regtest"] + list(args), stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, timeout=120)
        assert (result.returncode != 0) == fail, (args, result.returncode, result.stdout, result.stderr)
        return result.stderr if fail else result.stdout

    def info(self, text):
        return dict(line.split(": ", 1) for line in text.splitlines() if ": " in line)

    def run_test(self):
        node = self.nodes[0]
        node.generate(30)
        labelled = node.getnewaddress("savings")
        node.sendtoaddress(labelled, 5)
        node.generate(1)
        wallet_info = node.getwalletinfo()
        hd_seed = wallet_info["hdmasterkeyid"] if "hdmasterkeyid" in wallet_info else wallet_info.get("hdseedid")
        txcount = wallet_info["txcount"]
        wallet_dir = os.path.join(node.datadir, "regtest", "wallets")
        if not os.path.isdir(wallet_dir):
            wallet_dir = os.path.join(node.datadir, "regtest")
        wallet_file = os.path.join(wallet_dir, "wallet.dat")
        assert os.path.exists(wallet_file)

        self.log.info("A wallet that a running node uses is refused")
        assert "is a node using this wallet" in self.tool("-wallet=" + wallet_file, "info", fail=True)
        assert_equal(node.getwalletinfo()["txcount"], txcount)

        self.log.info("info and addresses of the wallet of a stopped node")
        self.stop_node(0)
        info = self.info(self.tool("-wallet=" + wallet_file, "info"))
        assert_equal(info["Encrypted"], "no")
        assert_equal(info["HD (hd seed available)"], "yes")
        if hd_seed:
            assert_equal(info["HD seed id"], hd_seed)
        assert_equal(int(info["Transactions"]), txcount)
        assert int(info["Keys"]) >= int(info["Key pool size"]) > 0
        addresses = self.tool("-wallet=" + wallet_file, "addresses")
        assert "%s\treceive\tsavings" % labelled in addresses.splitlines()
        every = self.tool("-wallet=" + wallet_file, "-allkeys", "addresses").splitlines()
        assert_equal(len(every), int(info["Keys"]))
        # by name, with the data directory
        assert_equal(self.info(self.tool("-datadir=" + node.datadir, "-wallet=wallet.dat", "info"))["Transactions"], info["Transactions"])
        # errors
        assert "There is no wallet file" in self.tool("-wallet=" + os.path.join(wallet_dir, "none.dat"), "info", fail=True)
        assert "-wallet=<wallet> is required" in self.tool("info", fail=True)
        assert "give one of the commands" in self.tool("-wallet=" + wallet_file, "nothing", fail=True)

        self.log.info("salvage keeps the keys")
        copy_dir = os.path.join(self.options.tmpdir, "copy")
        os.makedirs(copy_dir)
        shutil.copy(wallet_file, os.path.join(copy_dir, "wallet.dat"))
        self.tool("-wallet=" + os.path.join(copy_dir, "wallet.dat"), "salvage")
        salvaged = self.info(self.tool("-wallet=" + os.path.join(copy_dir, "wallet.dat"), "info"))
        assert_equal(salvaged["Keys"], info["Keys"])
        assert_equal(salvaged["HD seed id"], info["HD seed id"])
        assert any(name.endswith(".bak") for name in os.listdir(copy_dir)), os.listdir(copy_dir)

        self.log.info("The node loads its wallet again after the tool has read it")
        self.start_node(0)
        assert_equal(node.getwalletinfo()["txcount"], txcount)
        assert_equal(node.getaddressinfo(labelled)["label"], "savings")

        self.log.info("create makes a wallet that the node loads")
        created = os.path.join(wallet_dir, "made")
        made = self.info(self.tool("-wallet=" + created, "create"))
        assert_equal(made["Transactions"], "0")
        assert_equal(made["HD (hd seed available)"], "yes")
        assert "exists already" in self.tool("-wallet=" + created, "create", fail=True)
        first_key = self.tool("-wallet=" + created, "-allkeys", "addresses").splitlines()[0].split("\t")[0]
        first = node.get_wallet_rpc(node.listwallets()[0])
        assert_equal(node.loadwallet("made")["name"], "made")
        loaded = node.get_wallet_rpc("made")
        assert_equal(loaded.getaddressinfo(first_key)["ismine"], True)
        assert_equal(loaded.getwalletinfo()["txcount"], 0)
        address = loaded.getnewaddress()
        first.sendtoaddress(address, 3)
        first.generate(1)
        assert_equal(loaded.getbalance(), 3)
        node.unloadwallet("made")
        # unloaded, the file is free for the tool while the node runs
        assert_equal(self.info(self.tool("-wallet=" + created, "info"))["Transactions"], "1")


if __name__ == '__main__':
    ToolWalletTest().main()
