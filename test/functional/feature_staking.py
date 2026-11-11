#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test proof-of-stake block production and the wallet's handling of orphaned stakes.

regtest switches to proof-of-stake after -lastpowblock. Blocks are staked by
the wallet's staking thread; mocktime is advanced by one 16 second slot at a
time to let it find the next block.

 - the staking thread produces valid proof-of-stake blocks that a peer accepts
 - a stake that loses against a competing chain is abandoned, its input can be
   spent again, and it no longer counts in any balance (the wallet leaves such
   transactions out of its balance scans; -checkwalletlive, which the test
   framework turns on, verifies that against a full scan)
 - the orphaned stake counts again when its block is reconnected
 - purgeorphanstakes deletes the orphaned stakes from the wallet
"""

import os
import shutil

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    disconnect_nodes,
    sync_blocks,
    wait_until,
)

LAST_POW = 100
SLOT = 16


class StakingTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        args = ["-lastpowblock=%d" % LAST_POW, "-staking=1", "-emergencystaking=1"]
        self.extra_args = [args, args]

    def snapshot(self, node):
        info = node.getwalletinfo()
        return {
            "balance": info["balance"],
            "unconfirmed": info["unconfirmed_balance"],
            "immature": info["immature_balance"],
            "txcount": info["txcount"],
            "weight": node.getstakinginfo()["weight"],
            "unspent": sorted((u["txid"], u["vout"], u["amount"], u["spendable"]) for u in node.listunspent(0)),
        }

    def wait_snapshot(self, node, expected):
        # wallet notifications are asynchronous
        wait_until(lambda: self.snapshot(node) == expected, timeout=30)

    def set_time(self, nodes, t):
        for node in nodes:
            node.setmocktime(t)

    def stake(self, nodes, t):
        """Advance the given nodes to time t and wait until each staked a block."""
        heights = [node.getblockcount() for node in nodes]
        self.set_time(nodes, t)
        for node, height in zip(nodes, heights):
            wait_until(lambda: node.getblockcount() > height, timeout=30)

    def restart_node0(self, t):
        # start at the same mocktime, or the node stakes a block right away
        self.stop_node(0)
        self.start_node(0, self.extra_args[0] + ["-mocktime=%d" % t])

    def coinstake_of(self, node, blockhash):
        block = node.getblock(blockhash)
        assert "proof-of-stake" in block["flags"]
        return block["tx"][1]

    def run_test(self):
        n0, n1 = self.nodes
        t = n0.getblock(n0.getbestblockhash())["time"] + 600
        t -= t % SLOT
        self.set_time(self.nodes, t)

        self.log.info("Proof-of-work phase; give node1 coins that are old enough to stake")
        n0.generate(30)
        # a staked coin cannot stake again for 60 blocks, and coins of one
        # address are combined into one stake: use several addresses
        for _ in range(10):
            n0.sendtoaddress(n1.getnewaddress(), 100000)
        n0.generate(LAST_POW - 30)
        self.sync_all()
        assert_equal(n0.getblockcount(), LAST_POW)
        # no more proof-of-work blocks after this height
        assert_raises_rpc_error(-1, None, n0.generate, 1)
        assert_equal(n0.getblockcount(), LAST_POW)

        self.log.info("node0 stakes while node1 is partitioned away")
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        t = n0.getblock(n0.getbestblockhash())["time"] + 2 * SLOT
        t -= t % SLOT
        for i in range(3):
            if i == 2:
                below_tip = self.snapshot(n0)
            t += SLOT
            self.stake([n0], t)
        assert_equal(n0.getblockcount(), LAST_POW + 3)
        assert_equal(n1.getblockcount(), LAST_POW)
        base = self.snapshot(n0)

        self.log.info("A stake that is disconnected and connected again on the same parent")
        # (the staking thread finds the same, now invalid, block again and gives up)
        tip = n0.getbestblockhash()
        stake_tx = self.coinstake_of(n0, tip)
        n0.invalidateblock(tip)
        below_tip["txcount"] += 1  # the abandoned stake stays in the wallet
        self.wait_snapshot(n0, below_tip)
        assert n0.gettransaction(stake_tx)["confirmations"] <= 0
        n0.reconsiderblock(tip)
        wait_until(lambda: n0.getbestblockhash() == tip, timeout=30)
        self.wait_snapshot(n0, base)
        assert_equal(n0.gettransaction(stake_tx)["confirmations"], 1)

        self.log.info("A wallet file that still has a stake confirmed which the chain lost")
        # (without the staking thread: a restarted wallet may pick another
        # coin and stake a different, valid block in place of the invalid one)
        no_staking = ["-lastpowblock=%d" % LAST_POW, "-mocktime=%d" % t]
        wallet_file = os.path.join(self.options.tmpdir, "node0", "regtest", "wallets", "wallet.dat")
        self.stop_node(0)
        shutil.copyfile(wallet_file, wallet_file + ".ahead")
        self.start_node(0, no_staking)
        n0.invalidateblock(tip)
        self.wait_snapshot(n0, below_tip)
        self.stop_node(0)
        shutil.copyfile(wallet_file + ".ahead", wallet_file)
        self.start_node(0, no_staking)
        # abandoned at startup: same state as when the wallet saw the disconnect
        assert self.snapshot(n0) == below_tip
        assert_equal(n0.purgeorphanstakes(10 ** 9), 0)
        assert_equal(n0.purgeorphanstakes(0), 1)
        n0.reconsiderblock(tip)
        wait_until(lambda: n0.getbestblockhash() == tip, timeout=30)
        self.wait_snapshot(n0, base)
        self.restart_node0(t)

        self.log.info("node0 stakes two more blocks, node1 builds a longer chain")
        fork_a = []
        t0 = t
        for _ in range(2):
            t0 += SLOT
            self.stake([n0], t0)
            fork_a.append(n0.getbestblockhash())
        t1 = t
        for _ in range(LAST_POW + 7 - n1.getblockcount()):
            t1 += SLOT
            self.stake([n1], t1)
        assert_equal(n1.getblockcount(), LAST_POW + 7)
        fork_b_first = n1.getblockhash(LAST_POW + 1)

        # Catching up with node1's clock, node0 stakes one more block on its
        # own chain, which stays one block shorter than node1's
        t = t1
        self.stake([n0], t)
        fork_a.append(n0.getbestblockhash())
        assert_equal(n0.getblockcount(), LAST_POW + 6)
        stakes_a = [self.coinstake_of(n0, h) for h in fork_a]
        kernels_a = [(vin["txid"], vin["vout"]) for txid in stakes_a
                     for vin in n0.getrawtransaction(txid, True)["vin"]]
        on_fork_a = self.snapshot(n0)
        for txid in stakes_a:
            assert n0.gettransaction(txid)["confirmations"] > 0

        self.log.info("node0 reorganizes onto node1's chain: its stakes are orphaned")
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())
        for txid in stakes_a:
            assert n0.gettransaction(txid)["confirmations"] <= 0
        wait_until(lambda: all(k in set((u["txid"], u["vout"]) for u in n0.listunspent(0)) for k in kernels_a), timeout=30)
        on_fork_b = self.snapshot(n0)
        # node0's first three stakes were orphaned too, back to the proof-of-work balance
        assert on_fork_b["balance"] < base["balance"] or on_fork_b["immature"] < base["immature"]

        self.log.info("A restarted wallet computes the same state from scratch")
        self.restart_node0(t)
        assert self.snapshot(n0) == on_fork_b

        self.log.info("Reorganize back: the orphaned stakes count again")
        n0.invalidateblock(fork_b_first)
        wait_until(lambda: n0.getbestblockhash() == fork_a[-1], timeout=30)
        self.wait_snapshot(n0, on_fork_a)
        for txid in stakes_a:
            assert n0.gettransaction(txid)["confirmations"] > 0

        n0.reconsiderblock(fork_b_first)
        wait_until(lambda: n0.getbestblockhash() == n1.getbestblockhash(), timeout=30)
        self.wait_snapshot(n0, on_fork_b)

        self.log.info("purgeorphanstakes deletes the orphaned stakes and nothing else")
        for txid in stakes_a:
            assert n0.gettransaction(txid)["confirmations"] <= 0
        assert_equal(n0.purgeorphanstakes(86400), 0)
        removed = n0.purgeorphanstakes(0)
        assert removed >= len(stakes_a)
        for txid in stakes_a:
            assert_raises_rpc_error(-5, "Invalid or non-wallet transaction id", n0.gettransaction, txid)
        after = self.snapshot(n0)
        assert_equal(after["txcount"], on_fork_b["txcount"] - removed)
        after["txcount"] = on_fork_b["txcount"]
        assert after == on_fork_b
        assert all(tx["txid"] not in stakes_a for tx in n0.listtransactions("*", 1000))
        assert_equal(n0.purgeorphanstakes(0), 0)

        self.log.info("node0 keeps staking on the common chain and node1 follows")
        # two stakers that find a block in every slot would never converge
        self.stop_node(1)
        self.start_node(1, ["-lastpowblock=%d" % LAST_POW, "-mocktime=%d" % t])
        connect_nodes_bi(self.nodes, 0, 1)
        for _ in range(3):
            t += SLOT
            self.stake([n0], t)
            n1.setmocktime(t)
            sync_blocks(self.nodes)
        assert_equal(n0.getblockcount(), LAST_POW + 10)
        final = self.snapshot(n0)
        self.restart_node0(t)
        assert self.snapshot(n0) == final
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())


if __name__ == '__main__':
    StakingTest().main()
