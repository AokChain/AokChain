#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that orphan transactions are taken into the mempool when their parents arrive.

The orphans that wait for a transaction are processed one at a time, between
the messages of the peer that delivered the parent (CVE-2024-52914), not all
inside the handling of that one transaction. The outcome must be the same:
 - a chain of transactions sent last-first ends up in the mempool completely
 - many orphans of one parent are all taken in
 - an orphan whose parent is confirmed in a block is taken in as well
"""

from decimal import Decimal

from test_framework.mininode import CTransaction, FromHex, NetworkThread, NodeConn, NodeConnCB, msg_tx
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, p2p_port, wait_until


class OrphanProcessingTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def chain(self, node, utxo, length):
        """A chain of raw transactions, each spending the one before it. Not sent."""
        txs = []
        txid, vout, amount = utxo["txid"], utxo["vout"], utxo["amount"]
        for _ in range(length):
            amount -= Decimal("0.01")
            raw = node.createrawtransaction([{"txid": txid, "vout": vout}], {node.getnewaddress(): amount})
            signed = node.signrawtransaction(raw, [{"txid": txid, "vout": vout, "scriptPubKey": utxo["scriptPubKey"] if not txs else script, "amount": amount}] if txs else None)
            assert signed["complete"], signed
            decoded = node.decoderawtransaction(signed["hex"])
            script = decoded["vout"][0]["scriptPubKey"]["hex"]
            txs.append((decoded["txid"], signed["hex"]))
            txid, vout = decoded["txid"], 0
        return txs

    def run_test(self):
        maker, receiver = self.nodes
        maker.generate(40)
        self.sync_all()

        peer = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(1), receiver, peer)
        peer.add_connection(connection)
        NetworkThread().start()
        peer.wait_for_verack()

        def send(hex_tx):
            peer.send_message(msg_tx(FromHex(CTransaction(), hex_tx)))

        utxos = [u for u in maker.listunspent() if u["amount"] > 3 and u["amount"] < 100]
        assert len(utxos) >= 3

        self.log.info("A chain of 8 transactions, the last one first")
        chain = self.chain(maker, utxos[0], 8)
        for txid, hex_tx in reversed(chain[1:]):
            send(hex_tx)
        peer.sync_with_ping()
        assert_equal(len(receiver.getrawmempool()), 0)  # all orphans
        send(chain[0][1])
        wait_until(lambda: set(receiver.getrawmempool()) == set(txid for txid, _ in chain), timeout=30)

        self.log.info("Twenty orphans of one parent")
        outputs = {}
        while len(outputs) < 20:
            outputs[maker.getnewaddress()] = Decimal("0.1")
        parent_raw = maker.createrawtransaction([{"txid": utxos[1]["txid"], "vout": utxos[1]["vout"]}], outputs)
        parent = maker.signrawtransaction(parent_raw)
        assert parent["complete"]
        parent_decoded = maker.decoderawtransaction(parent["hex"])
        children = []
        for out in parent_decoded["vout"]:
            raw = maker.createrawtransaction([{"txid": parent_decoded["txid"], "vout": out["n"]}], {maker.getnewaddress(): Decimal("0.09")})
            signed = maker.signrawtransaction(raw, [{"txid": parent_decoded["txid"], "vout": out["n"], "scriptPubKey": out["scriptPubKey"]["hex"], "amount": out["value"]}])
            assert signed["complete"], signed
            children.append((maker.decoderawtransaction(signed["hex"])["txid"], signed["hex"]))
        before = set(receiver.getrawmempool())
        for txid, hex_tx in children:
            send(hex_tx)
        peer.sync_with_ping()
        assert_equal(set(receiver.getrawmempool()), before)
        send(parent["hex"])
        expected = before | {parent_decoded["txid"]} | set(txid for txid, _ in children)
        wait_until(lambda: set(receiver.getrawmempool()) == expected, timeout=30)
        # the peer is still served while the orphans are worked off
        peer.sync_with_ping()

        self.log.info("The mempool of the receiver is mined")
        for txid in receiver.getrawmempool():
            maker.sendrawtransaction(receiver.getrawtransaction(txid))
        maker.generate(1)
        self.sync_all()
        assert_equal(receiver.getrawmempool(), [])
        peer.sync_with_ping()  # the test peer is still connected: nobody was punished


if __name__ == '__main__':
    OrphanProcessingTest().main()
