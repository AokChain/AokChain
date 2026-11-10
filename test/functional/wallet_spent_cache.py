#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that the wallet's "fully spent transaction" cache never hides coins.

Balance and coin selection scans skip wallet transactions whose own outputs
have all been spent by confirmed transactions. That shortcut is memoized, so
it has to be dropped again when
 - an output that was not ours becomes ours (key import), or
 - the spending transaction stops being confirmed (reorg to a conflicting chain).
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, disconnect_nodes, sync_blocks


class WalletSpentCacheTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-maxreorg=10000"], ["-maxreorg=10000"]]

    def outputs_of(self, node, txid):
        """Unspent outputs of txid as the wallet sees them."""
        return sorted(u["vout"] for u in node.listunspent(0) if u["txid"] == txid)

    def send_raw(self, node, inputs, outputs, broadcast=True):
        raw = node.createrawtransaction(inputs, outputs)
        signed = node.signrawtransaction(raw)
        assert signed["complete"]
        if broadcast:
            return node.sendrawtransaction(signed["hex"])
        return signed["hex"]

    def vout_paying(self, node, txid, address):
        tx = node.getrawtransaction(txid, True)
        for out in tx["vout"]:
            if address in out["scriptPubKey"].get("addresses", []):
                return out["n"]
        raise AssertionError("no output to %s in %s" % (address, txid))

    def run_test(self):
        n0, n1 = self.nodes
        n0.generate(30)
        self.sync_all()

        self.log.info("Output that becomes ours after the transaction was cached as fully spent")
        ext_addr = n1.getnewaddress()
        ext_key = n1.dumpprivkey(ext_addr)
        fund_addr = n0.getnewaddress()
        fund = n0.sendtoaddress(fund_addr, 100)
        n0.generate(1)
        own = n0.getnewaddress()
        # T pays 10 to a foreign address and the rest back to this wallet
        t = self.send_raw(n0, [{"txid": fund, "vout": self.vout_paying(n0, fund, fund_addr)}],
                          {ext_addr: 10, own: Decimal("89.9")})
        n0.generate(1)
        # S spends the only output of T that is ours
        self.send_raw(n0, [{"txid": t, "vout": self.vout_paying(n0, t, own)}], {n0.getnewaddress(): Decimal("89.8")})
        n0.generate(1)
        assert_equal(self.outputs_of(n0, t), [])
        balance = n0.getbalance()  # T is now remembered as fully spent
        n0.getwalletinfo()
        n0.importprivkey(ext_key, "", False)
        assert_equal(self.outputs_of(n0, t), [self.vout_paying(n0, t, ext_addr)])
        assert_equal(n0.getbalance(), balance + 10)
        self.sync_all()

        self.log.info("Spending transaction is replaced by a conflicting chain")
        c_addr = n0.getnewaddress()
        u_addr = n0.getnewaddress()
        c = self.send_raw(n0, [{"txid": u["txid"], "vout": u["vout"]} for u in n0.listunspent() if u["amount"] == Decimal("89.8")],
                          {c_addr: Decimal("89.7")})
        u = n0.sendtoaddress(u_addr, 50)
        n0.generate(1)
        self.sync_all()
        c_in = {"txid": c, "vout": self.vout_paying(n0, c, c_addr)}
        u_in = {"txid": u, "vout": self.vout_paying(n0, u, u_addr)}
        # s spends C and U; s_conflict spends only U and is mined by the other node
        s_hex = self.send_raw(n0, [c_in, u_in], {n0.getnewaddress(): Decimal("139.6")}, broadcast=False)
        s_conflict_hex = self.send_raw(n0, [u_in], {n1.getnewaddress(): Decimal("49.9")}, broadcast=False)

        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        s = n0.sendrawtransaction(s_hex)
        n0.generate(1)
        assert_equal(self.outputs_of(n0, c), [])
        balance = n0.getbalance()  # C is now remembered as fully spent
        n0.getwalletinfo()
        n0.getstakinginfo()

        n1.sendrawtransaction(s_conflict_hex)
        n1.generate(3)
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)

        assert n0.gettransaction(s)["confirmations"] < 0
        # C's output is spendable again and counted
        assert_equal(self.outputs_of(n0, c), [c_in["vout"]])
        assert_equal(n0.getbalance(), sum(x["amount"] for x in n0.listunspent(0)))
        # and it can really be spent
        self.send_raw(n0, [c_in], {n0.getnewaddress(): Decimal("89.6")})
        n0.generate(1)
        assert_equal(self.outputs_of(n0, c), [])


if __name__ == '__main__':
    WalletSpentCacheTest().main()
