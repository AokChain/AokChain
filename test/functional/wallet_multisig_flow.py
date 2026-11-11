#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the multisig flow of the GUI wallet, step by step, through the RPCs it uses.

Three wallets make a 2-of-3 address from one public key each. The keys are
sorted (as bytes, BIP67 style), so that every wallet gets the same address
whatever the order in which the keys were entered. Each wallet watches the
address (importaddress with the redeem script). A spend is built from the
coins of the address with the change back to the address, signed by one
wallet, passed on, signed by a second wallet, combined and broadcast.

Negative cases: one signature is not enough, and a signature does not
survive a changed output.
"""

from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

COIN = 100000000


def signed_size(n_inputs, n_outputs, m, n):
    """Size in bytes of the fully signed transaction, as the GUI estimates it."""
    redeem = 3 + 34 * n                             # OP_m, n pushes of 33 bytes, OP_n, OP_CHECKMULTISIG
    push = 1 if redeem < 76 else 2 if redeem < 256 else 3
    script_sig = 1 + m * 73 + push + redeem         # OP_0, m signatures, the redeem script
    varint = 1 if script_sig < 253 else 3
    return 4 + 4 + 1 + n_inputs * (32 + 4 + varint + script_sig + 4) + 1 + n_outputs * (8 + 1 + 25) + 4


class WalletMultisigFlowTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def make_wallet(self, node, required, pubkeys, name):
        """What "create" and "add an invited multisig wallet" do."""
        created = node.createmultisig(required, sorted(pubkeys))
        node.importaddress(created["redeemScript"], name, False, True)
        return created

    def coins(self, node, address):
        """Coins of the address: largest first, then by txid and output number."""
        coins = node.listunspent(1, 9999999, [address])
        return sorted(coins, key=lambda c: (-c["amount"], c["txid"], c["vout"]))

    def build_spend(self, node, ms, to, amount, feerate):
        """The unsigned transaction: enough of the coins, the change back to the multisig address."""
        m, n = ms["m"], ms["n"]
        chosen, total = [], Decimal(0)
        for coin in self.coins(node, ms["address"]):
            chosen.append(coin)
            total += coin["amount"]
            fee = (feerate * signed_size(len(chosen), 2, m, n) / 1000).quantize(Decimal("0.00000001"))
            if total >= amount + fee:
                break
        else:
            raise AssertionError("insufficient funds")
        inputs = [{"txid": c["txid"], "vout": c["vout"]} for c in chosen]
        outputs = {to: amount}
        change = total - amount - fee
        if change > 0:
            outputs[ms["address"]] = change
        prevtxs = [{"txid": c["txid"], "vout": c["vout"], "scriptPubKey": c["scriptPubKey"],
                    "redeemScript": ms["redeemScript"], "amount": c["amount"]} for c in chosen]
        return node.createrawtransaction(inputs, outputs), prevtxs, fee

    def run_test(self):
        a, b, c = self.nodes
        a.generate(30)
        self.sync_all()

        self.log.info("One fresh key per wallet; the sorted keys give the same 2-of-3 address everywhere")
        addrs = [node.getnewaddress() for node in self.nodes]
        pubkeys = [node.validateaddress(addr)["pubkey"] for node, addr in zip(self.nodes, addrs)]
        for key in pubkeys:
            assert_equal(len(key), 66)  # compressed
        made = [self.make_wallet(a, 2, pubkeys, "ops"),
                self.make_wallet(b, 2, list(reversed(pubkeys)), "ops"),
                self.make_wallet(c, 2, [pubkeys[1], pubkeys[2], pubkeys[0]], "ops")]
        assert_equal(made[0], made[1])
        assert_equal(made[0], made[2])
        ms = {"address": made[0]["address"], "redeemScript": made[0]["redeemScript"], "m": 2, "n": 3}
        # the order of the keys does matter to the script: unsorted keys give another address
        unsorted = a.createmultisig(2, sorted(pubkeys, reverse=True))
        assert unsorted["address"] != ms["address"]
        decoded = a.decodescript(ms["redeemScript"])
        assert_equal(decoded["type"], "multisig")
        assert_equal(decoded["reqSigs"], 2)
        for node in self.nodes:
            info = node.validateaddress(ms["address"])
            assert_equal(info["isscript"], True)
            assert_equal(info["iswatchonly"], True)
            assert_equal(info["ismine"], False)

        self.log.info("Fund the address; every wallet sees the coins as watch-only")
        fund1 = a.sendtoaddress(ms["address"], 100)
        fund2 = a.sendtoaddress(ms["address"], 20)
        a.generate(1)
        self.sync_all()
        for node in self.nodes:
            coins = self.coins(node, ms["address"])
            assert_equal([coin["amount"] for coin in coins], [Decimal(100), Decimal(20)])
            assert_equal(coins[0]["txid"], fund1)
            assert_equal(coins[0]["spendable"], False)
        assert_equal(b.getbalance("*", 1, True), Decimal(120))
        assert_equal(b.getbalance(), Decimal(0))

        self.log.info("Wallet A builds the spend and signs: one signature of two")
        feerate = a.getnetworkinfo()["relayfee"] * 2
        to = c.getnewaddress()
        raw, prevtxs, fee = self.build_spend(a, ms, to, Decimal(30), feerate)
        tx = a.decoderawtransaction(raw)
        assert_equal(len(tx["vin"]), 1)
        assert_equal(tx["vin"][0]["txid"], fund1)
        paid = {out["scriptPubKey"]["addresses"][0]: out["value"] for out in tx["vout"]}
        assert_equal(paid, {to: Decimal(30), ms["address"]: Decimal(70) - fee})
        signed_a = a.signrawtransaction(raw, prevtxs)
        assert_equal(signed_a["complete"], False)
        assert signed_a["hex"] != raw

        self.log.info("One signature is not enough")
        assert_raises_rpc_error(-26, None, a.sendrawtransaction, signed_a["hex"])
        # signing again with the same wallet adds nothing
        again = a.signrawtransaction(signed_a["hex"], prevtxs)
        assert_equal(again["complete"], False)

        self.log.info("A changed output invalidates the signature of A")
        thief = b.getnewaddress()
        tampered_raw = a.createrawtransaction([{"txid": fund1, "vout": tx["vin"][0]["vout"]}],
                                              {thief: Decimal(30), ms["address"]: Decimal(70) - fee})
        # put the script with A's signature on the changed transaction
        script_sig = a.decoderawtransaction(signed_a["hex"])["vin"][0]["scriptSig"]["hex"]
        plain = a.decoderawtransaction(tampered_raw)
        assert_equal(plain["vin"][0]["scriptSig"]["hex"], "")
        marker = "%02x" % 0 + "ffffffff"
        position = tampered_raw.index(marker)
        length = len(script_sig) // 2
        assert length < 253
        tampered = tampered_raw[:position] + "%02x" % length + script_sig + tampered_raw[position + 2:]
        assert_equal(a.decoderawtransaction(tampered)["vin"][0]["scriptSig"]["hex"], script_sig)
        signed_tampered = b.signrawtransaction(tampered, prevtxs)
        # B's signature is fine for the changed transaction, A's is not: still incomplete, and refused
        assert_equal(signed_tampered["complete"], False)
        assert_raises_rpc_error(-26, None, b.sendrawtransaction, signed_tampered["hex"])

        self.log.info("Wallet B signs the real request: complete")
        signed_b = b.signrawtransaction(signed_a["hex"], prevtxs)
        assert_equal(signed_b["complete"], True)

        self.log.info("Signatures made side by side are combined")
        only_b = b.signrawtransaction(raw, prevtxs)
        assert_equal(only_b["complete"], False)
        only_c = c.signrawtransaction(raw, prevtxs)
        assert_equal(only_c["complete"], False)
        combined = a.combinerawtransaction([only_b["hex"], only_c["hex"]])
        check = a.signrawtransaction(combined, prevtxs)
        assert_equal(check["complete"], True)
        combined_ab = c.combinerawtransaction([signed_a["hex"], only_b["hex"]])
        assert_equal(combined_ab, signed_b["hex"])

        self.log.info("Broadcast, confirm, balances")
        txid = b.sendrawtransaction(signed_b["hex"])
        self.sync_all()
        b.generate(1)
        self.sync_all()
        assert_equal(a.getrawtransaction(txid, True)["confirmations"], 1)
        assert_equal(c.getbalance(), Decimal(30))
        for node in self.nodes:
            coins = self.coins(node, ms["address"])
            assert_equal(sorted(coin["amount"] for coin in coins), [Decimal(20), Decimal(70) - fee])
        # the other pair of signatures (B and C) spends the same coin: refused now
        assert combined != signed_b["hex"]
        assert_raises_rpc_error(-25, None, a.sendrawtransaction, combined)

        self.log.info("Everything that is left, without change")
        coins = self.coins(a, ms["address"])
        total = sum(coin["amount"] for coin in coins)
        fee_all = (feerate * signed_size(len(coins), 1, 2, 3) / 1000).quantize(Decimal("0.00000001"))
        raw_all = a.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]} for coin in coins],
                                         {to: total - fee_all})
        prev_all = [{"txid": coin["txid"], "vout": coin["vout"], "scriptPubKey": coin["scriptPubKey"],
                     "redeemScript": ms["redeemScript"], "amount": coin["amount"]} for coin in coins]
        half = c.signrawtransaction(raw_all, prev_all)
        assert_equal(half["complete"], False)
        full = a.signrawtransaction(half["hex"], prev_all)
        assert_equal(full["complete"], True)
        # the estimate is an upper bound of the real size, and close to it
        real = len(full["hex"]) // 2
        estimate = signed_size(len(coins), 1, 2, 3)
        assert real <= estimate and estimate - real <= 4 * len(coins) + 2, (real, estimate)
        a.sendrawtransaction(full["hex"])
        a.generate(1)
        self.sync_all()
        assert_equal(self.coins(b, ms["address"]), [])
        assert_equal(c.getbalance(), Decimal(30) + total - fee_all)


if __name__ == '__main__':
    WalletMultisigFlowTest().main()
