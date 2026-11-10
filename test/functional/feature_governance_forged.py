#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that a forged governance transaction cannot change governance state.

Governance actions (freeze a script, change token costs or the fee script)
are carried by a transaction that spends an output of the master address.
Anyone can create such an output. If that spend has an invalid scriptSig the
block must be rejected and the governance database must not change.

Before 3.1.0, nodes with -par > 1 did not enforce queued script checks at all
(doc/aokchain/04-security-audit.md SEC-01), so such a block was accepted and
the freeze applied. With the check restored but governance applied before
it, the block was rejected but the freeze stayed in the database.
"""

import time

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import COIN, COutPoint, CTransaction, CTxIn, CTxOut, ToHex
from test_framework.script import CScript
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes

# regtest strMasterAddress (src/chainparams.cpp)
MASTER_ADDRESS = "2NDM6PPx2FNYvgqvDdE8qnopCCD6kcGt14v"
OP_RETURN = 0x6a
GOVERNANCE_MARKER, GOVERNANCE_ACTION, GOVERNANCE_FREEZE = 71, 65, 70


class GovernanceForgedTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-par=1"], ["-par=4"]]

    def setup_network(self):
        self.setup_nodes()

    def check_node(self, node, label):
        node.generate(20)  # governance is active from height 10 on regtest
        victim = node.getnewaddress()
        victim_script = hex_str_to_bytes(node.validateaddress(victim)["scriptPubKey"])
        assert_equal(node.checkfreeze(victim), False)

        self.log.info("%s: fund the master address (anyone can)", label)
        txid = node.sendtoaddress(MASTER_ADDRESS, 1)
        node.generate(1)
        raw = node.getrawtransaction(txid, True)
        vout = [o["n"] for o in raw["vout"] if MASTER_ADDRESS in o["scriptPubKey"].get("addresses", [])][0]

        self.log.info("%s: forged governance freeze in a block", label)
        payload = bytes([GOVERNANCE_MARKER, GOVERNANCE_ACTION, GOVERNANCE_FREEZE, len(victim_script)]) + victim_script
        freeze_script = bytes([OP_RETURN, len(payload)]) + payload
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(txid, 16), vout), CScript([b"\x01" * 20]), 0xffffffff))
        tx.vout.append(CTxOut(0, freeze_script))

        tip = node.getbestblockhash()
        header = node.getblockheader(tip)
        block_time = max(int(time.time()), header["time"] + 1, tx.nTime)
        coinbase = create_coinbase(header["height"] + 1)
        coinbase.vout[0].nValue = 1 * COIN
        coinbase.nTime = block_time
        coinbase.rehash()
        block = create_block(int(tip, 16), coinbase, block_time)
        block.nBits = int(header["bits"], 16)
        block.nVersion = header["version"]
        tx.rehash()
        block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        result = node.submitblock(ToHex(block))
        self.log.info("%s: submitblock returned %r", label, result)
        assert result is not None, "block with a forged governance transaction was accepted"
        assert_equal(node.getbestblockhash(), tip)
        assert_equal(node.checkfreeze(victim), False)

    def run_test(self):
        self.check_node(self.nodes[0], "-par=1")
        self.check_node(self.nodes[1], "-par=4")


if __name__ == '__main__':
    GovernanceForgedTest().main()
