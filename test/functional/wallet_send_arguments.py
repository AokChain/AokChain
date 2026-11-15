#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the optional arguments of sendtoaddress and sendmany.

 - conf_target and estimate_mode are taken from the place that the help and
   the table of the commands give them, by position and by name
 - the message is written into the transaction (a version 2 transaction), at
   most 120 bytes
 - the help numbers the arguments in order
"""

import re

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class SendArgumentsTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def help_arguments(self, command):
        """The numbers and names of the arguments in the help of a command."""
        found = re.findall(r'^(\d+)\. +"?([a-z_]+)"?', self.nodes[0].help(command), re.M)
        return [(int(number), name) for number, name in found]

    def run_test(self):
        n0, n1 = self.nodes
        n0.generate(30)
        self.sync_all()
        to = n1.getnewaddress()

        self.log.info("sendtoaddress: conf_target and estimate_mode by position")
        txid = n0.sendtoaddress(to, 1, 0, "", "", False, 6)
        txid = n0.sendtoaddress(to, 1, 0, "", "", False, 6, "ECONOMICAL")
        assert txid in n0.getrawmempool()
        assert_raises_rpc_error(-8, "Invalid conf_target, must be between 1 - ", n0.sendtoaddress, to, 1, 0, "", "", False, 0)
        assert_raises_rpc_error(-8, "Invalid estimate_mode parameter", n0.sendtoaddress, to, 1, 0, "", "", False, 6, "SOMETIMES")

        self.log.info("sendtoaddress: conf_target and estimate_mode by name")
        n0.sendtoaddress(address=to, amount=1, conf_target=6)
        n0.sendtoaddress(address=to, amount=1, estimate_mode="CONSERVATIVE")
        n0.sendtoaddress(address=to, amount=1, conf_target=2, estimate_mode="ECONOMICAL")
        assert_raises_rpc_error(-8, "Invalid conf_target, must be between 1 - ", n0.sendtoaddress, address=to, amount=1, conf_target=0)
        assert_raises_rpc_error(-8, "Invalid estimate_mode parameter", n0.sendtoaddress, address=to, amount=1, estimate_mode="SOMETIMES")

        self.log.info("sendmany: conf_target and estimate_mode by position and by name")
        n0.sendmany("", {to: 1}, 1, "", [], 6)
        n0.sendmany("", {to: 1}, 1, "", [], 6, "ECONOMICAL")
        n0.sendmany(fromaccount="", amounts={to: 1}, conf_target=6, estimate_mode="CONSERVATIVE")
        assert_raises_rpc_error(-8, "Invalid conf_target, must be between 1 - ", n0.sendmany, "", {to: 1}, 1, "", [], 0)
        assert_raises_rpc_error(-8, "Invalid estimate_mode parameter", n0.sendmany, fromaccount="", amounts={to: 1}, estimate_mode="SOMETIMES")

        self.log.info("The message is written into the transaction")
        txid = n0.sendtoaddress(to, 1, 0, "for the coffee")
        tx = n0.getrawtransaction(txid, True)
        assert_equal(tx["version"], 2)
        assert_equal(tx["message"], "for the coffee")
        self.sync_all()
        assert_equal(n1.getrawtransaction(txid, True)["message"], "for the coffee")  # it travels with the transaction
        txid = n0.sendmany("", {to: 1}, 1, "paid together")
        assert_equal(n0.getrawtransaction(txid, True)["message"], "paid together")
        # at most 120 bytes: 40 Korean letters take 120 bytes
        n0.sendtoaddress(to, 1, 0, "a" * 120)
        n0.sendtoaddress(to, 1, 0, "\uac00" * 40)
        assert_raises_rpc_error(-3, "Transaction message max length is 120", n0.sendtoaddress, to, 1, 0, "a" * 121)
        assert_raises_rpc_error(-3, "Transaction message max length is 120", n0.sendtoaddress, to, 1, 0, "\uac00" * 41)
        # without a message the transaction keeps version 1
        txid = n0.sendtoaddress(to, 1)
        assert_equal(n0.getrawtransaction(txid, True)["version"], 1)

        self.log.info("The help numbers the arguments in the order of the table")
        assert_equal(self.help_arguments("sendtoaddress"), list(enumerate(
            ["address", "amount", "lock_time", "message", "comment_to", "subtractfeefromamount", "conf_target", "estimate_mode"], 1)))
        assert_equal(self.help_arguments("sendmany"), list(enumerate(
            ["fromaccount", "amounts", "minconf", "message", "subtractfeefrom", "conf_target", "estimate_mode"], 1)))
        for command in ("sendtoaddress", "sendmany"):
            usage = n0.help(command).split("\n")[0]
            assert "replaceable" not in usage, usage
        assert n0.help("sendtoaddress").startswith('sendtoaddress "address" amount ( lock_time "message" ')
        n0.generate(1)


if __name__ == '__main__':
    SendArgumentsTest().main()
