#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the governance commands of the master address.

A transaction that spends a coin of the governance master address may carry
commands in data outputs: freeze and unfreeze an address, set the cost of
issuing tokens, set the address that the cost is paid to. On regtest
-masteraddress replaces the built-in master address, so the test holds its key.

 - a frozen address cannot spend, in the mempool and in a block; the freeze
   is lifted by the unfreeze command
 - the same commands from any other address do nothing
 - the master address itself cannot be frozen
 - the issuing cost and the fee address change, and issuing a token then
   pays the new cost to the new address
 - commands are undone when their block is disconnected, done again when it
   is reconnected, and survive a restart
 - a command counts only in a block that is connected: a block that is refused
   (here: its coinbase pays too much), a block that is only checked (a block
   proposal of getblocktemplate, checked like a block template) and
   the check of the last blocks at startup (-checklevel=3, which disconnects
   them on a copy) leave the governance state as it was. Before, each of them
   changed it, and the node then refused valid blocks of the network.
"""

import struct
import time
from decimal import Decimal

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import COIN, CTransaction, FromHex, ToHex
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes_bi

FREEZE, UNFREEZE, COST, FEE = 70, 85, 67, 102
COST_ROOT = 1


def command(action, payload):
    """Data of a governance output: marker 'G', 'A', the action, and its argument."""
    return (bytes([71, 65, action]) + payload).hex()


def script_command(action, script_hex):
    script = bytes.fromhex(script_hex)
    return command(action, bytes([len(script)]) + script)


def cost_command(cost_type, amount):
    return command(COST, bytes([cost_type]) + struct.pack("<q", int(amount * 100000000)))


class GovernanceTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()
        return self.nodes[0].getbestblockhash()

    def script_of(self, address):
        return self.nodes[0].validateaddress(address)["scriptPubKey"]

    def sign_command(self, node, from_address, data):
        """A signed transaction that spends a coin of from_address, with the given data output."""
        coin = [u for u in node.listunspent(1, 9999999, [from_address]) if u["amount"] > 1][0]
        outputs = {"data": data, from_address: coin["amount"] - Decimal("0.01")}
        raw = node.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], outputs)
        signed = node.signrawtransactionwithwallet(raw)
        assert signed["complete"], signed
        return signed["hex"]

    def send_command(self, node, from_address, data):
        """Spend a coin of from_address in a transaction with the given data output."""
        txid = node.sendrawtransaction(self.sign_command(node, from_address, data))
        self.sync_all()
        return txid

    def block_with(self, node, tx_hex, coinbase_value):
        """A block on the tip with the transaction and a coinbase that pays coinbase_value."""
        tx = FromHex(CTransaction(), tx_hex)
        tip = node.getbestblockhash()
        header = node.getblockheader(tip)
        t = max(int(time.time()), header["time"] + 1, tx.nTime)
        coinbase = create_coinbase(header["height"] + 1)
        coinbase.nTime = t
        coinbase.vout[0].nValue = coinbase_value
        coinbase.rehash()
        block = create_block(int(tip, 16), coinbase, t)
        block.nBits = int(header["bits"], 16)
        block.nVersion = header["version"]
        block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return ToHex(block)

    def same_state(self):
        hashes = [node.getstatehashes()["governance"] for node in self.nodes]
        assert_equal(len(set(hashes)), 1)

    def frozen(self, node):
        return sorted(a for a, is_frozen in node.freezelist()["addresses"].items() if is_frozen)

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        master = n0.getnewaddress()
        other = n0.getnewaddress()
        victim = n1.getnewaddress()
        # one transaction: separate payments could spend each other's outputs
        n0.sendmany("", {master: 10000, other: 1000, victim: 1000})
        self.mine()

        self.log.info("Restart with the test's master address")
        self.stop_nodes()
        args = ["-masteraddress=" + master]
        self.extra_args = [args, args]
        self.start_nodes(self.extra_args)
        connect_nodes_bi(self.nodes, 0, 1)
        n0, n1 = self.nodes
        self.mine()
        assert_equal(self.frozen(n0), [])
        assert_equal(n1.checkfreeze(victim), False)
        defaults = n0.issuanceinfo()

        self.log.info("Commands of an address that is not the master address do nothing")
        self.send_command(n0, other, script_command(FREEZE, self.script_of(victim)))
        self.mine()
        self.send_command(n0, other, cost_command(COST_ROOT, Decimal(7)))
        self.mine()
        for node in self.nodes:
            assert_equal(self.frozen(node), [])
            assert_equal(node.issuanceinfo(), defaults)

        self.log.info("A refused block with a freeze command leaves the address as it was")
        freeze = self.sign_command(n0, master, script_command(FREEZE, self.script_of(victim)))
        tip = n0.getbestblockhash()
        # (100 AOK: more than the reward of a regtest block)
        assert_equal(n0.submitblock(self.block_with(n0, freeze, 100 * COIN)), "bad-cb-amount")
        assert_equal(n0.getbestblockhash(), tip)
        for node in self.nodes:
            assert_equal(self.frozen(node), [])
            assert_equal(node.checkfreeze(victim), False)
        self.same_state()

        self.log.info("A checked block proposal with the freeze command leaves the address as it was")
        # (getblocktemplate in proposal mode checks the block as block templates are checked,
        # with TestBlockValidity, which does not connect it)
        proposal = self.block_with(n0, freeze, 1 * COIN)
        assert_equal(n0.getblocktemplate({"mode": "proposal", "data": proposal}), None)
        assert_equal(n0.checkfreeze(victim), False)
        self.same_state()
        n0.sendrawtransaction(freeze)
        self.sync_all()

        self.log.info("Freeze: the same command in a block that is connected")
        freeze_block = self.mine()
        for node in self.nodes:
            assert_equal(self.frozen(node), [victim])
            assert_equal(node.checkfreeze(victim), True)
            assert_equal(node.checkfreeze(other), False)
        # the wallet of node1 has only the frozen coin
        assert_raises_rpc_error(None, None, n1.sendtoaddress, n0.getnewaddress(), 10)
        coin = n1.listunspent(1, 9999999, [victim])[0]
        raw = n1.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): 999})
        spend = n1.signrawtransactionwithwallet(raw)["hex"]
        assert_raises_rpc_error(-26, "bad-txns-address-frozen", n1.sendrawtransaction, spend)
        assert_raises_rpc_error(-26, "bad-txns-address-frozen", n0.sendrawtransaction, spend)

        self.log.info("The freeze is undone with its block, done again, and survives a restart")
        n0.invalidateblock(freeze_block)
        assert_equal(self.frozen(n0), [])
        assert_equal(n0.checkfreeze(victim), False)
        n0.reconsiderblock(freeze_block)
        assert_equal(n0.getbestblockhash(), freeze_block)
        assert_equal(self.frozen(n0), [victim])
        # node0 checks its last blocks at level 3 at the start (it disconnects them on a copy
        # of the coins, and does not connect them again); node1 at the default level
        self.stop_nodes()
        self.start_nodes([self.extra_args[0] + ["-checklevel=3", "-checkblocks=10"], self.extra_args[1]])
        connect_nodes_bi(self.nodes, 0, 1)
        n0, n1 = self.nodes
        for node in self.nodes:
            assert_equal(self.frozen(node), [victim])
        self.same_state()
        assert_raises_rpc_error(-26, "bad-txns-address-frozen", n1.sendrawtransaction, spend)

        self.log.info("The master address cannot be frozen")
        self.send_command(n0, master, script_command(FREEZE, self.script_of(master)))
        self.mine()
        assert_equal(self.frozen(n0), [victim])
        assert_equal(n0.checkfreeze(master), False)

        self.log.info("Unfreeze")
        self.send_command(n0, master, script_command(UNFREEZE, self.script_of(victim)))
        self.mine()
        for node in self.nodes:
            assert_equal(self.frozen(node), [])
            assert_equal(node.checkfreeze(victim), False)
        n1.sendrawtransaction(spend)
        self.sync_all()
        self.mine()
        assert_equal(n1.listunspent(1, 9999999, [victim]), [])

        self.log.info("Issuing cost and fee address")
        fee_address = n1.getnewaddress()
        self.send_command(n0, master, cost_command(COST_ROOT, Decimal(7)))
        cost_block = self.mine()
        self.send_command(n0, master, script_command(FEE, self.script_of(fee_address)))
        self.mine()
        for node in self.nodes:
            info = node.issuanceinfo()
            assert_equal(info["cost"]["root"], 7)
            assert_equal(info["cost"]["reissue"], defaults["cost"]["reissue"])
            assert_equal(info["address"], fee_address)
        assert_equal(n1.getreceivedbyaddress(fee_address), 0)
        n0.issue("GOVTEST", 100)
        self.mine()
        assert_equal(n1.getreceivedbyaddress(fee_address), 7)
        assert_equal(n1.gettokendata("GOVTEST")["amount"], 100)

        self.log.info("Disconnecting the blocks brings the old cost and address back")
        n0.invalidateblock(cost_block)
        assert_equal(n0.issuanceinfo(), defaults)
        n0.reconsiderblock(cost_block)
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())
        assert_equal(n0.issuanceinfo(), n1.issuanceinfo())
        assert_equal(n0.issuanceinfo()["cost"]["root"], 7)


if __name__ == '__main__':
    GovernanceTest().main()
