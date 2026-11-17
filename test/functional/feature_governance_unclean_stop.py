#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that the governance state follows the chain state over an unclean stop.

The governance database is written with every block that is connected or
disconnected, the chain state (the coins database) only at its flushes. A node
that is killed in between came back with the chain state of an earlier block
and the governance state of a later one, and judged the blocks in between with
it:

 - an address that is frozen later counted as frozen already: the block with
   an earlier payment of that address was refused as "bad-txns-address-frozen"
   and marked invalid, and the node could not follow the chain any more
 - a freeze whose block was disconnected after the last flush stayed undone,
   although the chain state came back with that block connected

The database now keeps what it needs to go back to the block of the last
flush of the chain state, and does so at startup.
"""

import struct
from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, sync_blocks, wait_until

FREEZE, UNFREEZE, COST = 70, 85, 67
COST_ROOT = 1


def command(action, payload):
    """Data of a governance output: marker 'G', 'A', the action, and its argument."""
    return (bytes([71, 65, action]) + payload).hex()


def script_command(action, script_hex):
    script = bytes.fromhex(script_hex)
    return command(action, bytes([len(script)]) + script)


def cost_command(cost_type, amount):
    return command(COST, bytes([cost_type]) + struct.pack("<q", int(amount * 100000000)))


class GovernanceUncleanStopTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine(self, n=1):
        self.nodes[0].generate(n)
        self.sync_all()
        return self.nodes[0].getbestblockhash()

    def send_command(self, node, from_address, data):
        """Spend a coin of from_address in a transaction with the given data output."""
        coin = [u for u in node.listunspent(1, 9999999, [from_address]) if u["amount"] > 1][0]
        outputs = {"data": data, from_address: coin["amount"] - Decimal("0.01")}
        raw = node.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], outputs)
        signed = node.signrawtransactionwithwallet(raw)
        assert signed["complete"], signed
        txid = node.sendrawtransaction(signed["hex"])
        self.sync_all()
        return txid

    def frozen(self, node):
        return sorted(a for a, is_frozen in node.freezelist()["addresses"].items() if is_frozen)

    def kill_node1(self):
        """Stop node1 without a flush, as a crash or a kill by the system does."""
        node = self.nodes[1]
        node.process.kill()
        node.process.wait()
        node.running = False
        node.process = None
        node.rpc_connected = False
        node.rpc = None

    def start_node1(self):
        self.start_node(1, self.extra_args[1])
        connect_nodes_bi(self.nodes, 0, 1)

    def same_state(self):
        hashes = [node.getstatehashes() for node in self.nodes]
        assert_equal(hashes[0]["governance"], hashes[1]["governance"])
        assert_equal(hashes[0]["utxo"], hashes[1]["utxo"])

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        master = n0.getnewaddress()
        victim = n1.getnewaddress()
        second = n1.getnewaddress()
        # one transaction: separate payments could spend each other's outputs
        n0.sendmany("", {master: 10000, victim: 1000, second: 1000})
        self.mine()

        self.stop_nodes()
        args = ["-masteraddress=" + master]
        self.extra_args = [args, args]
        self.start_nodes(self.extra_args)
        connect_nodes_bi(self.nodes, 0, 1)
        n0, n1 = self.nodes
        self.mine(3)
        default_cost = n0.issuanceinfo()

        self.log.info("A freeze after the last flush of the chain state, then a kill")
        # gettxoutsetinfo writes the chain state: node1's coins database is at this block
        n1.gettxoutsetinfo()
        flushed = n1.getbestblockhash()
        # a payment of the address, then its freeze and a new issuing cost, in later blocks
        coin = n1.listunspent(1, 9999999, [victim])[0]
        raw = n1.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): 100, victim: coin["amount"] - 101})
        n1.sendrawtransaction(n1.signrawtransactionwithwallet(raw)["hex"])
        self.sync_all()
        self.mine()
        self.send_command(n0, master, script_command(FREEZE, n0.validateaddress(victim)["scriptPubKey"]))
        self.mine()
        self.send_command(n0, master, cost_command(COST_ROOT, Decimal(7)))
        self.mine()
        self.mine(2)
        for node in self.nodes:
            assert_equal(self.frozen(node), [victim])
            assert_equal(node.checkfreeze(victim), True)
        changed_cost = n0.issuanceinfo()
        assert changed_cost != default_cost
        assert_equal(n1.issuanceinfo(), changed_cost)
        tip = n0.getbestblockhash()

        # (no getstatehashes or gettxoutsetinfo on node1 here: they write the chain state)
        self.kill_node1()
        self.start_node1()
        n1 = self.nodes[1]
        # It came back at the block of the flush or a later one that it had on disk, and
        # connects the rest again: the block with the payment must not meet the freeze.
        with open(n1.datadir + "/regtest/debug.log", encoding="utf8") as log:
            loaded = [line for line in log if "Loaded best chain" in line][-1]
        assert "hashBestChain=%s " % flushed in loaded, loaded
        sync_blocks(self.nodes, timeout=60)
        assert_equal(n1.getbestblockhash(), tip)
        assert_equal(self.frozen(n1), [victim])
        assert_equal(n1.issuanceinfo(), changed_cost)
        self.same_state()
        assert_equal([t for t in n1.getchaintips() if t["status"] == "invalid"], [])

        self.log.info("A freeze that is disconnected after the last flush, then a kill")
        self.send_command(n0, master, script_command(FREEZE, n0.validateaddress(second)["scriptPubKey"]))
        freeze_block = self.mine()
        assert_equal(self.frozen(n1), sorted([victim, second]))
        n1.gettxoutsetinfo()  # the chain state of node1 is written with the freeze
        n1.invalidateblock(freeze_block)
        assert_equal(self.frozen(n1), [victim])
        self.kill_node1()
        self.start_node1()
        n1 = self.nodes[1]
        # The chain state is that of the block with the freeze again (the invalidation was
        # not written either): so is the governance state.
        wait_until(lambda: n1.getbestblockhash() == freeze_block, timeout=30)
        assert_equal(self.frozen(n1), sorted([victim, second]))
        self.same_state()

        self.log.info("Nothing to undo after a clean stop")
        self.send_command(n0, master, script_command(UNFREEZE, n0.validateaddress(second)["scriptPubKey"]))
        self.mine()
        assert_equal(self.frozen(n1), [victim])
        self.stop_node(1)
        self.start_node1()
        n1 = self.nodes[1]
        sync_blocks(self.nodes)
        assert_equal(self.frozen(n1), [victim])
        self.same_state()
        # and the chain goes on with it
        self.mine(2)
        self.same_state()


if __name__ == '__main__':
    GovernanceUncleanStopTest().main()
