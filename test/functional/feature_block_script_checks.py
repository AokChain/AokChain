#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that blocks containing transactions with invalid scripts are rejected.

Script checks of a block are queued to worker threads when -par > 1 (the
default on multi-core machines) and are only verified by
CCheckQueueControl::Wait() in ConnectBlock(). That check had been commented
out, so such nodes accepted blocks with invalid signatures
(doc/aokchain/04-security-audit.md SEC-01).

For both -par=1 (inline checks) and -par=4 (queued checks):
- a block spending a coin with a corrupted signature must be rejected
- the same block with a correctly signed transaction must be accepted
"""

import time

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import COIN, CTransaction, FromHex, ToHex
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes


class BlockScriptChecksTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-par=1"], ["-par=4"]]

    def setup_network(self):
        # Keep the nodes apart; each one is tested on its own.
        self.setup_nodes()

    def build_block(self, node, spend_tx):
        tip = node.getbestblockhash()
        tip_header = node.getblockheader(tip)
        # nTime is covered by the signature, so keep the transaction's own time
        # and choose a block time that is not earlier than it.
        block_time = max(int(time.time()), tip_header["time"] + 1, spend_tx.nTime)
        coinbase = create_coinbase(tip_header["height"] + 1)
        coinbase.vout[0].nValue = 1 * COIN
        coinbase.nTime = block_time
        coinbase.rehash()
        block = create_block(int(tip, 16), coinbase, block_time)
        block.nBits = int(tip_header["bits"], 16)
        block.nVersion = tip_header["version"]
        spend_tx.rehash()
        block.vtx.append(spend_tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return block

    def signed_spend(self, node, corrupt):
        utxo = [u for u in node.listunspent() if u["amount"] > 1][0]
        raw = node.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}],
                                        {node.getnewaddress(): 1})
        signed = node.signrawtransaction(raw)
        assert signed["complete"]
        tx = FromHex(CTransaction(), signed["hex"])
        if corrupt:
            # Flip a bit inside the DER-encoded R value of the signature
            # (scriptSig = <sig> <pubkey>; byte 0 is the push length, then
            # 0x30 len 0x02 rlen R...), keeping the encoding well-formed.
            script = bytearray(tx.vin[0].scriptSig)
            script[10] ^= 0x01
            tx.vin[0].scriptSig = bytes(script)
        return tx

    def check_node(self, node, label):
        self.log.info("%s: mine mature coins", label)
        node.generate(20)

        self.log.info("%s: block with an invalid signature is rejected", label)
        tip = node.getbestblockhash()
        bad_block = self.build_block(node, self.signed_spend(node, corrupt=True))
        result = node.submitblock(ToHex(bad_block))
        self.log.info("%s: submitblock returned %r", label, result)
        assert result is not None, "block with an invalid signature was accepted"
        # Make sure it was rejected for its script, not for something else.
        assert result in ("block-validation-failed", "mandatory-script-verify-flag-failed") or \
            result.startswith("mandatory-script-verify-flag-failed"), result
        assert_equal(node.getbestblockhash(), tip)

        self.log.info("%s: same block with a valid signature is accepted", label)
        good_block = self.build_block(node, self.signed_spend(node, corrupt=False))
        assert_equal(node.submitblock(ToHex(good_block)), None)
        assert_equal(node.getbestblockhash(), good_block.hash)

    def run_test(self):
        self.check_node(self.nodes[0], "-par=1")
        self.check_node(self.nodes[1], "-par=4")


if __name__ == '__main__':
    BlockScriptChecksTest().main()
