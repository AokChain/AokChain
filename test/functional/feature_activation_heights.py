#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that outputs with an activation height are checked against the height of their block.

Offline staking outputs, tokens and tokens on script hash addresses are valid
from a height on (10 on regtest). A block is checked against its own height,
wherever the active chain of the node stands at that moment:
 - a block below the height with an offline staking output is refused, the
   same kind of block above the height is accepted
 - blocks above the height are accepted when they are read back from disk
   (-reindex and -reindex-chainstate): the tip and the hashes of the chain
   state are the ones from before
 - and when a peer delivers them ahead of the tip, before the blocks between,
   to a node whose chain is still below the height; the peer stays connected
"""

import time

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import (
    CBlock,
    CBlockHeader,
    COIN,
    FromHex,
    NetworkThread,
    NodeConn,
    NodeConnCB,
    ToHex,
    msg_headers,
    msg_witness_block,
)
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, p2p_port, wait_until

ACTIVATION_HEIGHT = 10

# OP_OFFLINE_STAKE OP_IF <pay to a key hash> OP_ELSE <pay to a key hash> OP_ENDIF
OFFLINE_STAKING_SCRIPT = (bytes([0xc6, 0x63, 0x76, 0xa9, 0x14]) + b"\x11" * 20 + bytes([0x88, 0xac]) +
                          bytes([0x67, 0x76, 0xa9, 0x14]) + b"\x22" * 20 + bytes([0x88, 0xac, 0x68]))


class ActivationHeightsTest(AokChainTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def setup_network(self):
        # The nodes stay apart: each part of the test feeds its node by hand.
        self.setup_nodes()

    def block_paying_to(self, node, script):
        """The next block of the node's chain, with a coinbase output to the script."""
        tip = node.getbestblockhash()
        header = node.getblockheader(tip)
        block_time = max(int(time.time()), header["time"] + 1)
        coinbase = create_coinbase(header["height"] + 1)
        coinbase.vout[0].nValue = 1 * COIN
        coinbase.vout[0].scriptPubKey = script
        coinbase.nTime = block_time
        coinbase.rehash()
        block = create_block(int(tip, 16), coinbase, block_time)
        block.nBits = int(header["bits"], 16)
        block.nVersion = header["version"]
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return block

    def wait_for_height(self, node, height):
        wait_until(lambda: node.getblockcount() == height, timeout=120)

    def run_test(self):
        maker, low, late = self.nodes

        self.log.info("Below the activation height a block with an offline staking output is refused")
        low.generate(ACTIVATION_HEIGHT - 3)
        tip = low.getbestblockhash()
        block = self.block_paying_to(low, OFFLINE_STAKING_SCRIPT)
        assert_equal(low.submitblock(ToHex(block)), "offline-staking-not-enabled")
        assert_equal(low.getbestblockhash(), tip)

        self.log.info("The same kind of block above the activation height is accepted")
        # (from the activation height on a block has another version: the block is made on
        # top of a block that has it)
        low.generate(3)
        assert_equal(low.getblockcount(), ACTIVATION_HEIGHT)
        block = self.block_paying_to(low, OFFLINE_STAKING_SCRIPT)
        assert_equal(low.submitblock(ToHex(block)), None)
        assert_equal(low.getbestblockhash(), block.hash)

        self.log.info("A chain with an offline staking output, a token and a token on a script hash address")
        maker.generate(30)
        offline = maker.getofflinestakingaddress(maker.getnewaddress(), maker.getnewaddress())
        maker.sendtoaddress(offline, 1000)
        offline_block = maker.generate(1)[0]
        maker.issue("HEIGHTTEST", 1000)
        token_block = maker.generate(1)[0]
        script_hash_address = maker.addmultisigaddress(1, [maker.getnewaddress()])
        maker.transfer("HEIGHTTEST", 10, script_hash_address)
        script_hash_block = maker.generate(1)[0]
        maker.generate(3)
        height, best = maker.getblockcount(), maker.getbestblockhash()
        hashes = maker.getstatehashes()
        assert_equal(maker.listaddressesbytoken("HEIGHTTEST")[script_hash_address], 10)
        ahead = [offline_block, token_block, script_hash_block]

        for option in ["-reindex", "-reindex-chainstate"]:
            self.log.info("The blocks are read back from disk with %s" % option)
            self.stop_node(0)
            self.start_node(0, extra_args=[option])
            self.wait_for_height(maker, height)
            assert_equal(maker.getbestblockhash(), best)
            assert_equal(maker.getstatehashes(), hashes)
            assert_equal([tip["status"] for tip in maker.getchaintips()], ["active"])

        self.log.info("A peer delivers the three blocks ahead of the tip of a node below the activation height")
        blocks = [FromHex(CBlock(), maker.getblock(maker.getblockhash(h), False)) for h in range(1, height + 1)]
        by_hash = {maker.getblockhash(h): blocks[h - 1] for h in range(1, height + 1)}
        start = ACTIVATION_HEIGHT - 5
        for block in blocks[:start]:
            assert_equal(late.submitblock(ToHex(block)), None)
        assert_equal(late.getblockcount(), start)

        peer = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(2), late, peer)
        peer.add_connection(connection)
        NetworkThread().start()
        peer.wait_for_verack()
        peer.send_message(msg_headers([CBlockHeader(block) for block in blocks[start:]]))
        peer.sync_with_ping()
        wait_until(lambda: any(tip["height"] == height for tip in late.getchaintips()), timeout=30)

        for block_hash in ahead:
            peer.send_message(msg_witness_block(by_hash[block_hash]))
        peer.sync_with_ping()  # the peer is still connected: no block was refused
        assert_equal(late.getblockcount(), start)
        for block_hash in ahead:
            # (the block is stored: its transactions are known although it is not connected yet)
            assert_equal(late.getblock(block_hash)["confirmations"], -1)

        self.log.info("The blocks between arrive, and the node follows the whole chain")
        for block in blocks[start:]:
            if block.hash not in ahead:
                peer.send_message(msg_witness_block(block))
        peer.sync_with_ping()
        self.wait_for_height(late, height)
        assert_equal(late.getbestblockhash(), best)
        assert_equal(late.getstatehashes(), hashes)
        peer.sync_with_ping()


if __name__ == '__main__':
    ActivationHeightsTest().main()
