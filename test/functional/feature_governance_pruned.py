#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test a pruned node whose records of what the governance commands replaced are not there.

A node that is not pruned answers a missing record by building its databases again from its
block files (feature_governance_reorg.py, feature_auto_rebuild.py). A pruned node cannot do
that. It does not guess either:

 - a record is missing for a block that it is asked to give up: the block stays and the command
   reports an error; the node stops when a longer branch asks for it; the log says why and
   what helps; the next start finds the missing record by its check of the last blocks and
   ends with the same advice; -reindex helps;
 - the database of the records is gone: the start ends and names the two ways on. With the
   file governanceundo.since removed, the node goes on with its databases, which were not
   found wanting, and keeps the records from its current block on.

node0 has the master key, makes the commands and is the node to compare with; node1 is pruned.
"""

import os
import shutil
from decimal import Decimal

from test_framework.mininode import CTransaction, CTxOut, FromHex, ToHex
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, connect_nodes_bi, disconnect_nodes, sync_blocks

FREEZE, UNFREEZE = 70, 85
STATE = ("utxo", "utxo_legacy", "tokens", "governance", "index")
PRUNED = ["-prune=1", "-txindex=0"]
NOT_ON_RECORD = "replaced is not on record"
CANNOT_REBUILD = "This node is pruned and cannot build them again from its own block files"
TWO_WAYS = "remove the file governanceundo.since from the data directory and start again"
FROM_NOW_ON = "is on record for the blocks connected from now on"


class GovernancePrunedTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], PRUNED]

    def script_command(self, action, address):
        script = bytes.fromhex(self.nodes[0].validateaddress(address)["scriptPubKey"])
        return bytes([71, 65, action, len(script)]) + script

    def master_tx(self, commands):
        n0 = self.nodes[0]
        coin = self.master_coins.pop()
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {self.master: coin["amount"] - Decimal("0.01")})
        tx = FromHex(CTransaction(), raw)
        for data in commands:
            tx.vout.append(CTxOut(0, bytes([0x6a, len(data)]) + data))
        signed = n0.signrawtransactionwithwallet(ToHex(tx))
        assert signed["complete"], signed
        return n0.sendrawtransaction(signed["hex"])

    def mine(self, n=1):
        self.nodes[0].generate(n)
        sync_blocks(self.nodes)
        return self.nodes[0].getbestblockhash()

    def governance(self, node):
        return {"freeze": node.freezelist(), "issuance": node.issuanceinfo(), "hash": node.getstatehashes()["governance"]}

    def same_state(self):
        sync_blocks(self.nodes)
        n0, n1 = self.nodes
        assert_equal(self.governance(n1), self.governance(n0))
        hashes = [node.getstatehashes() for node in self.nodes]
        for part in STATE:
            assert_equal(hashes[1][part], hashes[0][part])

    def log_count(self, text):
        with open(os.path.join(self.nodes[1].datadir, "regtest", "debug.log"), encoding="utf8") as log:
            return log.read().count(text)

    def start1(self, extra_args=None):
        self.start_node(1, self.args1 + (extra_args or []), stderr=self.stderr1)
        connect_nodes_bi(self.nodes, 0, 1)

    def run_test(self):
        n0, n1 = self.nodes
        self.mine(30)
        self.master = n0.getnewaddress()
        a, b = n0.getnewaddress(), n0.getnewaddress()
        for _ in range(12):
            n0.sendtoaddress(self.master, 10)
            n0.lockunspent(False, [{"txid": u["txid"], "vout": u["vout"]} for u in n0.listunspent(0, 9999999, [self.master])])
        self.mine()
        self.stop_nodes()
        self.args1 = ["-masteraddress=" + self.master] + PRUNED
        self.stderr1 = open(os.path.join(self.options.tmpdir, "node1.stderr"), "w")
        self.start_node(0, ["-masteraddress=" + self.master])
        self.start1()
        n0, n1 = self.nodes
        self.master_coins = n0.listunspent(1, 9999999, [self.master])
        assert len(self.master_coins) >= 12
        assert_equal(n1.getblockchaininfo()["pruned"], True)

        self.master_tx([self.script_command(FREEZE, a)])
        self.mine()
        assert_equal(n1.checkfreeze(a), True)
        self.same_state()

        self.log.info("A record is missing for a block that the pruned node is asked to give up: the block stays")
        # (a command that changes nothing: giving it up by reverting the command would release a)
        self.master_tx([self.script_command(FREEZE, a)])
        block = self.mine()
        assert_equal(n1.removegovernanceundo(block), True)
        state = self.governance(n1)
        assert_raises_rpc_error(-20, "cannot be disconnected", n1.invalidateblock, block)
        assert_equal(n1.getbestblockhash(), block)
        assert_equal([tip for tip in n1.getchaintips() if tip["status"] == "invalid"], [])
        assert_equal(self.governance(n1), state)
        assert_equal(self.log_count(NOT_ON_RECORD), 1)
        assert_equal(self.log_count(CANNOT_REBUILD), 1)
        self.same_state()

        self.log.info("A longer branch asks for it: the node stops, and says in its log why and what helps")
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        n0.invalidateblock(block)
        n0.generate(2)
        try:
            connect_nodes(n0, 1)
        except Exception:
            pass   # (the pruned node may be gone before the connection is counted)
        n1.process.wait(timeout=120)
        self.log.info("the pruned node ended with exit code %d" % n1.process.returncode)
        n1.running = False
        n1.process = None
        n1.rpc_connected = False
        n1.rpc = None
        assert_equal(self.log_count(NOT_ON_RECORD), 2)
        assert_equal(self.log_count(CANNOT_REBUILD), 2)
        assert_equal(self.log_count("Failed to disconnect block"), 1)
        # The check of the last blocks at a start gives each of them up in memory: it finds the
        # missing record as well, and the start ends with the way on for a pruned node.
        for _ in range(2):
            self.assert_start_raises_init_error(1, self.args1, "This node is pruned: it cannot build its databases again from its own block files")
        assert_equal(self.log_count(NOT_ON_RECORD), 4)
        # -reindex is that way (here the block files are all there; a node that has pruned
        # some downloads the blocks again)
        self.start1(["-reindex"])
        n1 = self.nodes[1]
        self.same_state()
        assert_equal([tip for tip in n1.getchaintips() if tip["status"] == "invalid"], [])
        self.stop_node(1)
        self.start1()
        self.same_state()

        self.log.info("The database of the records is gone on a pruned node: the start names the two ways on")
        datadir1 = os.path.join(n1.datadir, "regtest")
        since = os.path.join(datadir1, "governanceundo.since")
        self.master_tx([self.script_command(FREEZE, b)])
        self.mine()
        self.stop_node(1)
        assert os.path.isfile(since)
        shutil.rmtree(os.path.join(datadir1, "governanceundo"))
        for _ in range(2):
            self.assert_start_raises_init_error(1, self.args1, TWO_WAYS)
        from_now_on = self.log_count(FROM_NOW_ON)
        os.remove(since)
        self.start1()
        n1 = self.nodes[1]
        assert_equal(self.log_count(FROM_NOW_ON), from_now_on + 1)
        assert os.path.isfile(since)
        self.same_state()

        self.log.info("It keeps the records from that block on")
        before = self.governance(n1)
        self.master_tx([self.script_command(FREEZE, b)])   # changes nothing
        block = self.mine()
        n1.invalidateblock(block)
        assert_equal(self.governance(n1), before)
        assert_equal(n1.checkfreeze(b), True)
        n1.reconsiderblock(block)
        self.same_state()
        self.stderr1.close()


if __name__ == '__main__':
    GovernancePrunedTest().main()
