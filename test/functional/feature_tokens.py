#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test tokens: issue, transfer, reissue, unique tokens, and what is refused.

 - a token is issued with its owner token; both nodes see the same data
 - transfers move amounts between wallets; the sum over all holders is always
   the issued amount
 - only the holder of the owner token reissues; a token that is not
   reissuable is not reissued
 - unique tokens are issued below a root and below a sub token
 - amounts finer than the units of the token, more than the balance, a name
   that exists and a protected name are refused
 - tokens are not sent to an offline staking address, with an error that
   says so
 - a username is @ followed by at least 4 characters of A-Z 0-9 _ . (where
   the punctuation stands is not checked), and the error of registerusername
   says so
 - the token state survives a restart
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class TokensTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()

    def holders_sum(self, node, name):
        return sum(Decimal(str(v)) for v in node.listaddressesbytoken(name).values())

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        # (a transfer of tokens pays its network fee in coins: both wallets need some)
        n0.sendtoaddress(n1.getnewaddress(), 1000000)
        self.mine()
        fee_address_balance = n0.getbalance()

        self.log.info("Issue a token")
        address0 = n0.getnewaddress()
        n0.issue("TESTCOIN", 1000, address0, "", 4, True)
        self.mine()
        assert n0.getbalance() < fee_address_balance  # issuing costs coins
        for node in (n0, n1):
            data = node.gettokendata("TESTCOIN")
            assert_equal(data["name"], "TESTCOIN")
            assert_equal(data["amount"], 1000)
            assert_equal(data["units"], 4)
            assert_equal(data["reissuable"], 1)
        mine = n0.listmytokens("TESTCOIN*")
        assert_equal(mine["TESTCOIN"], 1000)
        assert_equal(mine["TESTCOIN!"], 1)   # the owner token
        assert_equal(n1.listmytokens("TESTCOIN*"), {})
        assert_equal(n0.listtokenbalancesbyaddress(address0)["TESTCOIN"], 1000)
        assert "TESTCOIN" in n1.listtokens()

        self.log.info("Transfer")
        address1 = n1.getnewaddress()
        n0.transfer("TESTCOIN", 200, address1)
        self.mine()
        assert_equal(n1.listmytokens("TESTCOIN")["TESTCOIN"], 200)
        assert_equal(n0.listmytokens("TESTCOIN")["TESTCOIN"], 800)
        assert_equal(n0.listtokenbalancesbyaddress(address1)["TESTCOIN"], 200)
        assert_equal(self.holders_sum(n0, "TESTCOIN"), Decimal("1000"))
        # and back, a part of it, with the smallest unit
        n1.transfer("TESTCOIN", Decimal("50.0001"), n0.getnewaddress())
        self.sync_all()
        self.mine()
        assert_equal(n1.listmytokens("TESTCOIN")["TESTCOIN"], Decimal("149.9999"))
        assert_equal(n0.listmytokens("TESTCOIN")["TESTCOIN"], Decimal("850.0001"))
        assert_equal(self.holders_sum(n1, "TESTCOIN"), Decimal("1000"))

        self.log.info("What a transfer refuses")
        assert_raises_rpc_error(None, None, n1.transfer, "TESTCOIN", 150, n0.getnewaddress())            # more than the balance
        assert_raises_rpc_error(None, None, n1.transfer, "TESTCOIN", Decimal("0.00001"), n0.getnewaddress())  # finer than 4 units
        assert_raises_rpc_error(None, None, n1.transfer, "TESTCOIN", 0, n0.getnewaddress())
        assert_raises_rpc_error(None, None, n1.transfer, "TESTCOIN", -1, n0.getnewaddress())
        assert_raises_rpc_error(None, None, n1.transfer, "NOSUCHTOKEN", 1, n0.getnewaddress())
        assert_raises_rpc_error(None, None, n1.transfer, "TESTCOIN", 1, "notanaddress")
        self.mine()
        assert_equal(self.holders_sum(n0, "TESTCOIN"), Decimal("1000"))

        self.log.info("Tokens do not go to an offline staking address, and the error says so")
        # (its script takes no token data; consensus refuses such an output)
        offline = n0.getofflinestakingaddress(n1.getnewaddress(), n0.getnewaddress())
        refused = "Tokens cannot be sent to an offline staking address: " + offline
        assert_raises_rpc_error(-5, refused, n0.transfer, "TESTCOIN", 1, offline)
        holder = [a for a in n0.listaddressesbytoken("TESTCOIN") if n0.validateaddress(a)["ismine"]][0]
        assert_raises_rpc_error(-5, refused, n0.transferfromaddress, holder, "TESTCOIN", 1, offline)
        assert_raises_rpc_error(-5, refused, n0.transfermany, "TESTCOIN", {offline: 1})
        assert_raises_rpc_error(-5, refused, n0.transfermanyoutputs, [{"address": offline, "token_name": "TESTCOIN", "amount": 1, "locktime": 0}])
        assert_raises_rpc_error(-5, refused, n0.issue, "OFFLINECOIN", 10, offline)
        assert_raises_rpc_error(-5, refused, n0.issueunique, "TESTCOIN", ["offline"], None, offline)
        assert_raises_rpc_error(-5, refused, n0.reissue, "TESTCOIN", 10, offline)
        assert_raises_rpc_error(-5, refused, n0.registerusername, "@OFFLINEUSER", offline)
        assert_raises_rpc_error(-5, refused, n0.createrawtransaction, [], {offline: {"transfer": {"TESTCOIN": 1}}})
        # the owner token of a sub token, a unique token and a reissue comes back to the change address
        assert_raises_rpc_error(-5, refused + " (the change address, which receives the owner token)", n0.issue, "TESTCOIN/OFFLINE", 1, address0, offline)
        assert_raises_rpc_error(-5, refused + " (the change address, which receives the owner token)", n0.reissue, "TESTCOIN", 10, address0, offline)
        assert_equal(n0.getrawmempool(), [])
        n0.sendtoaddress(offline, 10)  # coins still go there
        self.mine()
        assert_equal(self.holders_sum(n0, "TESTCOIN"), Decimal("1000"))

        self.log.info("Reissue: only with the owner token")
        assert_raises_rpc_error(None, None, n1.reissue, "TESTCOIN", 500, n1.getnewaddress())
        n0.reissue("TESTCOIN", 500, address0)
        self.mine()
        for node in (n0, n1):
            assert_equal(node.gettokendata("TESTCOIN")["amount"], 1500)
        assert_equal(self.holders_sum(n1, "TESTCOIN"), Decimal("1500"))
        assert_equal(n0.listmytokens("TESTCOIN")["TESTCOIN"], Decimal("1350.0001"))

        self.log.info("A token that is not reissuable")
        n0.issue("FIXEDCOIN", 21, address0, "", 0, False)
        self.mine()
        assert_equal(n1.gettokendata("FIXEDCOIN")["reissuable"], 0)
        assert_raises_rpc_error(None, None, n0.reissue, "FIXEDCOIN", 1, address0)
        assert_raises_rpc_error(None, None, n0.transfer, "FIXEDCOIN", Decimal("0.5"), address1)  # no units
        self.mine()
        assert_equal(n1.gettokendata("FIXEDCOIN")["amount"], 21)

        self.log.info("Names that are refused")
        assert_raises_rpc_error(None, None, n1.issue, "TESTCOIN", 5)       # exists
        assert_raises_rpc_error(None, None, n0.issue, "AOK", 5)            # protected
        assert_raises_rpc_error(None, None, n0.issue, "AOKCHAIN", 5)
        assert_raises_rpc_error(None, None, n0.issue, "ab", 5)             # too short, lower case
        assert_raises_rpc_error(None, None, n1.issue, "TESTCOIN/SUB", 5)   # a sub token needs the owner token

        self.log.info("Unique tokens below a root")
        n0.issueunique("TESTCOIN", ["one", "two"])
        self.mine()
        unique = n0.listmytokens("TESTCOIN#*")
        assert_equal(unique, {"TESTCOIN#one": 1, "TESTCOIN#two": 1})
        n0.transfer("TESTCOIN#one", 1, address1)
        self.mine()
        assert_equal(n1.listmytokens("TESTCOIN#*"), {"TESTCOIN#one": 1})
        assert_raises_rpc_error(None, None, n0.issueunique, "TESTCOIN", ["one"])   # exists
        assert_raises_rpc_error(None, None, n1.issueunique, "TESTCOIN", ["three"]) # no owner token

        self.log.info("Unique tokens below a sub token, with the owner token of the sub token")
        n0.issue("TESTCOIN/SUB", 10, address0)
        self.mine()
        n0.issueunique("TESTCOIN/SUB", ["alpha", "beta"])
        self.mine()
        assert_equal(n0.listmytokens("TESTCOIN/SUB#*"), {"TESTCOIN/SUB#alpha": 1, "TESTCOIN/SUB#beta": 1})
        assert_equal(n0.listmytokens("TESTCOIN/SUB!"), {"TESTCOIN/SUB!": 1})  # the owner token stays
        assert_equal(n1.gettokendata("TESTCOIN/SUB#alpha")["amount"], 1)
        assert_raises_rpc_error(-32600, "Wallet doesn't have token: TESTCOIN/SUB!", n1.issueunique, "TESTCOIN/SUB", ["gamma"])
        assert_raises_rpc_error(-8, "Name is greater than max length of 31", n0.issueunique, "TESTCOIN/SUB", ["x" * 20])
        assert_raises_rpc_error(-8, "Root token must be a regular top-level or sub-token", n0.issueunique, "TESTCOIN/SUB#alpha", ["x"])

        self.log.info("Usernames: @ followed by at least 4 characters of A-Z 0-9 _ .")
        rules = "A username is @ followed by at least 4 characters (valid characters are: A-Z 0-9 _ .)"
        for name in ("@abcd", "@ABC", "@AB-CD", "ABCDE", "TESTCOIN"):
            assert_raises_rpc_error(-8, "Invalid username: %s\nError: %s" % (name, rules), n0.registerusername, name)
        assert_raises_rpc_error(-8, "Name is greater than max length of 31", n0.registerusername, "@" + "A" * 31)
        # where a dot or an underscore stands is not checked (consensus): these are usernames
        usernames = ["@AB..CD", "@.ABCD", "@ABCD_", "@" + "A" * 30]
        for name in usernames:
            n0.registerusername(name)
        self.mine()
        for name in usernames:
            assert_equal(n1.gettokendata(name)["amount"], 1)

        self.log.info("The owner token can be handed over")
        n0.transfer("TESTCOIN!", 1, address1)
        self.mine()
        assert_raises_rpc_error(None, None, n0.reissue, "TESTCOIN", 1, address0)
        n1.reissue("TESTCOIN", 100, address1)
        self.sync_all()
        self.mine()
        assert_equal(n0.gettokendata("TESTCOIN")["amount"], 1600)
        assert_equal(self.holders_sum(n0, "TESTCOIN"), Decimal("1600"))

        self.log.info("The token state survives a restart and is the same on both nodes")
        before = {name: n0.gettokendata(name) for name in ("TESTCOIN", "FIXEDCOIN", "TESTCOIN#one")}
        balances = n0.listmytokens()
        self.stop_node(0)
        self.start_node(0)
        assert_equal({name: n0.gettokendata(name) for name in before}, before)
        assert_equal(n0.listmytokens(), balances)
        for name in before:
            assert_equal(n1.gettokendata(name), before[name])
            assert_equal(n0.listaddressesbytoken(name), n1.listaddressesbytoken(name))

        self.log.info("The coin change of a token issue still goes to an offline staking address")
        # (last: the wallet does not spend its coins there unasked, and the change is most of them)
        txid = n0.issue("CHANGECOIN", 10, address0, offline)[0]
        n0.generate(1)  # (node 0 is not connected again after its restart)
        assert_equal(n0.gettokendata("CHANGECOIN")["amount"], 10)
        offline_script = n0.validateaddress(offline)["scriptPubKey"]
        assert offline_script in [out["scriptPubKey"]["hex"] for out in n0.getrawtransaction(txid, True)["vout"]]


if __name__ == '__main__':
    TokensTest().main()
