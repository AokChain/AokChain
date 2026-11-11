#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test importmnemonic: restore the keys of another wallet from its recovery words.

Coins are sent to addresses that mnemonictokeys derives (that RPC is checked
against an independent implementation in rpc_mnemonic.py); a second node then
restores them from the words alone.
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, sync_blocks

WORDS = "legal winner thank year wave sausage worth useful legal winner thank yellow"
KOREAN = "가격 가격 가격 가격 가격 가격 가격 가격 가격 가격 가격 가능"
OTHER = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"


class ImportMnemonicTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def address(self, words, path, index, passphrase=""):
        return self.nodes[0].mnemonictokeys(words, path, 1, passphrase, index)["keys"][0]["address"]

    def run_test(self):
        funder, node, encrypted = self.nodes
        funder.generate(110)

        self.log.info("Fund addresses of the recovery words on several paths")
        pay = {
            self.address(WORDS, "m/44'/0'/0'/0", 0): 10,      # BIP44, first receiving address
            self.address(WORDS, "m/44'/0'/0'/0", 31): 2,      # past the first 20: found by following the path
            self.address(WORDS, "m/44'/0'/0'/1", 4): 3,       # change chain
            self.address(WORDS, "m/44'/175'/0'/0", 2): 5,     # another common coin type
            self.address(WORDS, "m/0'/0'", 7): 1,             # not hardened child of m/0'/0'
            self.address(KOREAN, "m/44'/0'/0'/0", 1, "비밀"): 7,
            self.address(OTHER, "m/44'/1999'/0'/0", 3): 4,    # unusual coin type: needs the address
        }
        funder.sendmany("", pay)
        funder.generate(1)
        sync_blocks(self.nodes)

        self.log.info("Errors")
        assert_raises_rpc_error(-8, "checksum", node.importmnemonic, WORDS.replace("yellow", "year"))
        assert_raises_rpc_error(-4, "No coins were found", node.importmnemonic, OTHER)
        assert_raises_rpc_error(-4, "No coins were found", node.importmnemonic, WORDS, "wrong passphrase")
        assert_raises_rpc_error(-5, "pay-to-pubkey-hash", node.importmnemonic, WORDS, "", "nonsense")
        assert_equal(node.getbalance(), 0)

        self.log.info("Restore: the coins of all usual paths appear after the rescan")
        res = node.importmnemonic(WORDS)
        assert_equal(res["language"], "english")
        found = {entry["address"]: entry for entry in res["found"]}
        assert_equal(len(found), 5)
        for address, amount in list(pay.items())[:5]:
            assert_equal(found[address]["amount"], amount)
            assert node.validateaddress(address)["ismine"]
        assert_equal(found[self.address(WORDS, "m/44'/0'/0'/0", 31)]["path"], "m/44'/0'/0'/0/31")
        assert_equal(node.getbalance(), Decimal(21))
        # the gap limit: 20 places past the last used one, on the receiving and the change chain
        assert node.validateaddress(self.address(WORDS, "m/44'/0'/0'/0", 51))["ismine"]
        assert not node.validateaddress(self.address(WORDS, "m/44'/0'/0'/0", 52))["ismine"]
        assert node.validateaddress(self.address(WORDS, "m/44'/0'/0'/1", 24))["ismine"]
        assert not node.validateaddress(self.address(WORDS, "m/44'/0'/0'/1", 25))["ismine"]
        assert node.validateaddress(self.address(WORDS, "m/44'/175'/0'/1", 0))["ismine"]
        # a path without coins is not imported
        assert not node.validateaddress(self.address(WORDS, "m/44'/1'/0'/0", 0))["ismine"]
        # only the addresses with coins are in the address book
        assert_equal(sorted(node.getaddressesbyaccount("restored")), sorted(found))

        self.log.info("A second import adds nothing")
        again = node.importmnemonic(WORDS, "", "", False)
        assert_equal(again["imported"], 0)

        self.log.info("The restored coins can be spent")
        node.sendtoaddress(funder.getnewaddress(), 20)
        self.sync_all()
        funder.generate(1)
        sync_blocks(self.nodes)
        assert node.getbalance() < 1

        # nothing is left on these words now
        assert_raises_rpc_error(-4, "No coins were found", self.nodes[2].importmnemonic, WORDS)

        self.log.info("Korean words with a passphrase")
        res = node.importmnemonic(KOREAN, "비밀")
        assert_equal(res["language"], "korean")
        assert_equal(len(res["found"]), 1)
        assert_equal(res["found"][0]["amount"], 7)

        self.log.info("An unusual path is found with an address of that wallet, used or not")
        unused = self.address(OTHER, "m/44'/1999'/0'/0", 0)
        before = node.getbalance()
        res = node.importmnemonic(OTHER, "", unused)
        assert_equal(len(res["found"]), 1)
        assert_equal(res["found"][0]["path"], "m/44'/1999'/0'/0/3")
        assert_equal(node.getbalance(), before + 4)
        assert node.validateaddress(unused)["ismine"]
        assert_raises_rpc_error(-4, "does not belong", node.importmnemonic, OTHER, "x", unused)

        self.log.info("An encrypted wallet has to be unlocked, and not for staking only")
        encrypted.node_encrypt_wallet("pass")
        self.start_node(2)
        connect_nodes(encrypted, 0)
        sync_blocks(self.nodes)
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", encrypted.importmnemonic, KOREAN, "비밀")
        encrypted.walletpassphrase("pass", 600, True)
        assert_raises_rpc_error(-13, "staking only", encrypted.importmnemonic, KOREAN, "비밀")
        encrypted.walletpassphrase("pass", 600)
        res = encrypted.importmnemonic(KOREAN, "비밀")
        assert_equal(encrypted.getbalance(), 7)
        encrypted.walletlock()
        # the keys were stored encrypted: they are there after a restart, and need the passphrase
        self.stop_node(2)
        self.start_node(2)
        assert_equal(encrypted.getbalance(), 7)
        assert_raises_rpc_error(-13, "Please enter the wallet passphrase", encrypted.dumpprivkey, res["found"][0]["address"])


if __name__ == '__main__':
    ImportMnemonicTest().main()
