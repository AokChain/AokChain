#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that a block that is disconnected leaves the governance state as it was before the block.

node0 has the master key and makes the commands, node1 follows it, node2 is cut off while the
commands are mined and mines a longer branch without them. When the nodes meet again, node0 and
node1 give up the block with the commands. Their freeze list, issuing costs, fee address and
state hashes must then be those of node2, which never saw the block:

 - a command that changed nothing (an address frozen again, an address released again);
 - a command for an address that was not known before;
 - several commands for one address, in one transaction and in two, in either order;
 - two cost commands of one kind in one block, and a fee address.

Then "there and back": a block with a payment from an address is given up for a branch that
releases the address (which changes nothing) and freezes it, and that branch is given up again.
The block with the payment, which the nodes had connected before, connects again.

What a block replaced is on record from the moment the node connects it. The last part is
about the blocks without a record:
 - a data directory as a version without the records leaves it: its blocks are given up as
   they always were, and the blocks connected from then on have their records;
 - a record that is missing where the node connected the block itself: the node does not
   guess. It stops, builds its databases again at the next start, and has the state of the
   others.
"""

import os
import shutil
import struct
from decimal import Decimal

from test_framework.mininode import CTransaction, CTxOut, FromHex, ToHex
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes, connect_nodes_bi, disconnect_nodes, sync_blocks

FREEZE, UNFREEZE, COST, FEE = 70, 85, 67, 102
COST_ROOT, COST_SUB = 1, 4
STATE = ("utxo", "utxo_legacy", "tokens", "governance", "index")


def command(action, payload):
    """Data of a governance output: marker 'G', 'A', the action, and its argument."""
    return bytes([71, 65, action]) + payload


def cost_command(kind, amount):
    return command(COST, bytes([kind]) + struct.pack("<q", int(amount * 100000000)))


class GovernanceReorgTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def script_command(self, action, address):
        script = bytes.fromhex(self.nodes[0].validateaddress(address)["scriptPubKey"])
        return command(action, bytes([len(script)]) + script)

    def master_tx(self, commands, send_to=None):
        """One transaction of the master address with these commands, an output each, in this order."""
        n0 = self.nodes[0]
        coin = self.master_coins.pop()
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {self.master: coin["amount"] - Decimal("0.01")})
        tx = FromHex(CTransaction(), raw)
        for data in commands:
            tx.vout.append(CTxOut(0, bytes([0x6a, len(data)]) + data))  # OP_RETURN and one push
        signed = n0.signrawtransactionwithwallet(ToHex(tx))
        assert signed["complete"], signed
        return (send_to or n0).sendrawtransaction(signed["hex"])

    def mine(self, n=1, node=None, peers=None):
        (node or self.nodes[0]).generate(n)
        sync_blocks(peers if peers is not None else self.nodes)

    def split(self):
        for i in (0, 1):
            disconnect_nodes(self.nodes[i], 2)
            disconnect_nodes(self.nodes[2], i)

    def join(self):
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes)

    def governance(self, node):
        return {"freeze": node.freezelist(), "issuance": node.issuanceinfo(), "hash": node.getstatehashes()["governance"]}

    def same_as_node2(self, what):
        n2 = self.nodes[2]
        reference = self.governance(n2)
        hashes = n2.getstatehashes()
        for node in self.nodes[:2]:
            assert_equal(self.governance(node), reference)
            got = node.getstatehashes()
            for part in STATE:
                assert_equal(got[part], hashes[part])
        self.log.info("%s: node0 and node1 have the governance state of node2 (%d addresses known, %d frozen)" % (
            what, len(reference["freeze"]["addresses"]), sum(1 for f in reference["freeze"]["addresses"].values() if f)))

    def wait_exit(self, i, code):
        """The node ends by itself, with this exit code."""
        node = self.nodes[i]
        node.process.wait(timeout=240)
        assert_equal(node.process.returncode, code)
        node.running = False
        node.process = None
        node.rpc_connected = False
        node.rpc = None

    def log_count(self, i, text):
        with open(os.path.join(self.nodes[i].datadir, "regtest", "debug.log"), encoding="utf8") as log:
            return log.read().count(text)

    def run_test(self):
        n0, n1, n2 = self.nodes
        self.mine(30)
        self.master = n0.getnewaddress()
        a, b, c, d, e, f, g, h, x = [n0.getnewaddress() for _ in range(9)]
        n0.sendtoaddress(x, 50)
        self.mine()
        for _ in range(40):
            n0.sendtoaddress(self.master, 10)
            # (the wallet must not pay the next one with these)
            n0.lockunspent(False, [{"txid": u["txid"], "vout": u["vout"]} for u in n0.listunspent(0, 9999999, [self.master, x])])
        self.mine()
        self.stop_nodes()
        self.extra_args = [["-masteraddress=" + self.master]] * 3
        self.start_nodes(self.extra_args)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 1, 2)
        n0, n1, n2 = self.nodes
        self.master_coins = n0.listunspent(1, 9999999, [self.master])
        assert len(self.master_coins) >= 40

        self.log.info("The state before: a, e frozen; b, f, g, x known and not frozen; c, d not known")
        for address in (a, b, e, f, g, x):
            self.master_tx([self.script_command(FREEZE, address)])
        self.mine()
        for address in (b, f, g, x):
            self.master_tx([self.script_command(UNFREEZE, address)])
        self.mine()
        before = self.governance(n2)
        assert_equal(before["freeze"]["addresses"][a], True)
        assert_equal(before["freeze"]["addresses"][b], False)
        assert c not in before["freeze"]["addresses"]
        self.same_as_node2("before")

        self.log.info("node2 is cut off; node0 mines a block with commands, node1 follows")
        self.split()
        self.master_tx([self.script_command(FREEZE, a)])                                       # changes nothing
        self.master_tx([self.script_command(UNFREEZE, b)])                                     # changes nothing
        self.master_tx([self.script_command(UNFREEZE, c)])                                     # not known before
        self.master_tx([self.script_command(FREEZE, d)])                                       # not known before
        self.master_tx([self.script_command(UNFREEZE, e), self.script_command(FREEZE, e)])     # two in one transaction
        self.master_tx([self.script_command(FREEZE, f), self.script_command(UNFREEZE, f)])     # ...in the other order
        self.master_tx([self.script_command(UNFREEZE, g)])                                     # two in one block: nothing,
        self.master_tx([self.script_command(FREEZE, g)])                                       # then a freeze
        self.master_tx([cost_command(COST_ROOT, 7), cost_command(COST_ROOT, 8), cost_command(COST_SUB, 9)])
        self.master_tx([self.script_command(FEE, h)])
        self.mine(1, peers=[n0, n1])
        assert_equal(n0.getrawmempool(), [])
        with_block = self.governance(n1)
        assert_equal(with_block["freeze"]["addresses"][d], True)
        assert_equal(with_block["freeze"]["addresses"][c], False)
        assert_equal(with_block["freeze"]["addresses"][e], True)
        assert_equal(with_block["freeze"]["addresses"][f], False)
        assert_equal(with_block["issuance"]["cost"]["root"], 7)
        assert_equal(with_block["issuance"]["cost"]["sub"], 9)
        assert_equal(with_block["issuance"]["address"], h)
        assert with_block["hash"] != before["hash"]

        self.log.info("node2's longer branch: node0 and node1 give the block up")
        self.mine(2, node=n2, peers=[n2])
        assert_equal(self.governance(n2), before)
        self.join()
        self.same_as_node2("after the block was disconnected")
        assert_equal(self.governance(n0), before)

        self.log.info("The commands are mined on the new branch after all, and that block is given up as well")
        # (they came back to the memory pool of node0 and node1)
        self.split()
        self.mine(1, peers=[n0, n1])
        assert self.governance(n0) != before
        self.mine(2, node=n2, peers=[n2])
        self.join()
        self.same_as_node2("after the second disconnect")
        assert_equal(self.governance(n0), before)

        self.log.info("A restart keeps it")
        self.stop_node(1)
        self.start_node(1, self.extra_args[1])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.mine()   # (the commands, for everyone this time)
        self.same_as_node2("with the commands in the chain of all three")

        self.log.info("There and back: a payment from x; a branch that releases x again and freezes it; back to the payment")
        self.split()
        coin = n0.listunspent(1, 9999999, [x])[0]
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): 10, x: coin["amount"] - Decimal("10.01")})
        payment = n0.sendrawtransaction(n0.signrawtransactionwithwallet(raw)["hex"])
        self.mine(1, peers=[n0, n1])
        with_payment = n0.getbestblockhash()
        assert payment in n0.getblock(with_payment)["tx"]
        # node2 gets the two commands for x (the master key is node0's) and mines them
        self.master_tx([self.script_command(UNFREEZE, x)], send_to=n2)   # changes nothing
        self.mine(1, node=n2, peers=[n2])
        self.master_tx([self.script_command(FREEZE, x)], send_to=n2)
        self.mine(2, node=n2, peers=[n2])
        other = n2.getblockhash(n0.getblockcount())
        self.join()
        assert_equal(n0.checkfreeze(x), True)
        self.same_as_node2("on the branch that freezes x")
        asked = [self.log_count(i, "The databases of this node are not what they were") for i in (0, 1)]
        for node in (n0, n1):
            node.invalidateblock(other)
            # x is as it was before that branch, and the block with the payment connects again
            assert_equal(node.getbestblockhash(), with_payment)
            assert_equal(node.checkfreeze(x), False)
        assert_equal(self.governance(n0), self.governance(n1))
        for node in (n0, n1):
            node.reconsiderblock(other)
        sync_blocks(self.nodes)
        assert_equal([self.log_count(i, "The databases of this node are not what they were") for i in (0, 1)], asked)
        self.same_as_node2("there and back")

        self.log.info("A data directory as a version without the records leaves it")
        datadir1 = os.path.join(n1.datadir, "regtest")
        since = os.path.join(datadir1, "governanceundo.since")
        records = os.path.join(datadir1, "governanceundo")
        assert os.path.isdir(records) and os.path.isfile(since)
        # a block with a command that changes something, for an address that is known: the
        # one case in which the revert of the command itself is the state before
        self.master_tx([self.script_command(FREEZE, b)])
        self.mine()
        old_block = n0.getbestblockhash()
        self.mine()
        taken_over = self.log_count(1, "is on record for the blocks connected from now on")
        self.stop_node(1)
        shutil.rmtree(records)
        os.remove(since)
        stderr1 = open(os.path.join(self.options.tmpdir, "node1.stderr"), "w")
        self.start_node(1, self.extra_args[1], stderr=stderr1)
        n1 = self.nodes[1]
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        assert_equal(self.log_count(1, "is on record for the blocks connected from now on"), taken_over + 1)
        assert os.path.isfile(since)
        assert_equal(n1.removegovernanceundo(old_block), False)   # (there is none)
        self.split()
        self.mine(3, node=n2, peers=[n2])        # node2 has the block with the command and goes on
        n1.invalidateblock(old_block)            # node1 gives it up, the way it always did
        assert_equal(n1.checkfreeze(b), False)
        assert_equal(n1.getblockcount(), n0.getblockcount() - 2)
        assert_equal(self.log_count(1, "The databases of this node are not what they were"), asked[1])
        n1.reconsiderblock(old_block)
        self.join()
        self.same_as_node2("blocks from before the records")

        self.log.info("Blocks connected since have their records")
        self.split()
        self.master_tx([self.script_command(FREEZE, b), self.script_command(UNFREEZE, a), self.script_command(UNFREEZE, a)])
        self.mine(1, peers=[n0, n1])
        new_block = n1.getbestblockhash()
        self.mine(2, node=n2, peers=[n2])
        self.join()
        self.same_as_node2("a block with a record, given up")
        self.mine()   # (the commands again, for everyone)
        self.same_as_node2("with those commands in the chain of all three")

        self.log.info("A record is missing where the node connected the block itself: it stops, and builds its databases again")
        self.split()
        self.master_tx([self.script_command(UNFREEZE, b), self.script_command(FREEZE, c)])
        self.mine(1, peers=[n0, n1])
        damaged = n1.getbestblockhash()
        assert new_block != damaged
        assert_equal(n1.removegovernanceundo(damaged), True)
        assert_equal(n1.removegovernanceundo(damaged), False)
        self.mine(2, node=n2, peers=[n2])
        connect_nodes(n2, 1)                     # node2's branch asks node1 to give the block up
        self.wait_exit(1, 1)
        assert_equal(self.log_count(1, "replaced is not on record"), 1)
        assert_equal(self.log_count(1, "The databases of this node are not what they were"), asked[1] + 1)
        assert_equal(self.log_count(1, "was decided at an earlier run and is not done"), 0)
        self.start_node(1, self.extra_args[1], stderr=stderr1)
        assert_equal(self.log_count(1, "was decided at an earlier run and is not done"), 1)
        self.join()
        self.same_as_node2("after the node built its databases again")
        # every block has its record again: the blocks from before the records too
        assert_equal(self.nodes[1].removegovernanceundo(old_block), True)
        stderr1.close()


if __name__ == '__main__':
    GovernanceReorgTest().main()
