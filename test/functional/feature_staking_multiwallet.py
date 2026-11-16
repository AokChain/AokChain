#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test staking with several wallets in one node.

Every loaded wallet has a staking thread of its own. regtest switches to
proof-of-stake after -lastpowblock; mocktime is advanced by one 16 second slot
at a time, and the node stakes one block in each slot.

 - with -staking the wallets stake, each with its own coins, and a peer
   accepts their blocks
 - an unloaded wallet stakes no more; the others go on
 - a wallet that is created or loaded while the node runs stakes too
 - a locked wallet does not stake while another one does, and
   "walletpassphrase <passphrase> 0 true" opens it for staking only
 - getstakinginfo answers for the wallet of the request
 - without -staking no wallet stakes
"""

import time

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    sync_blocks,
    wait_until,
)

LAST_POW = 100
SLOT = 16
COINS = 16
PASSPHRASE = "passphrase of the first wallet"


class StakingMultiWalletTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.pos_args = ["-lastpowblock=%d" % LAST_POW, "-emergencystaking=1"]
        self.extra_args = [self.pos_args + ["-staking=1"], self.pos_args]

    def set_time(self, t):
        for node in self.nodes:
            node.setmocktime(t)

    def staker_of(self, blockhash):
        """The wallet (its key in self.wallets) whose stake made this block."""
        block = self.nodes[0].getblock(blockhash)
        assert "proof-of-stake" in block["flags"], block["flags"]
        stake = block["tx"][1]
        owners = []

        def find():
            # (wallet notifications are asynchronous)
            del owners[:]
            for name, wallet in self.wallets.items():
                try:
                    wallet.gettransaction(stake)
                    owners.append(name)
                except JSONRPCException:
                    pass
            return len(owners) > 0

        wait_until(find, timeout=30)
        assert_equal(len(owners), 1)
        return owners[0]

    def stake(self, count):
        """Advance the time slot by slot: node0 stakes one block in each.

        Returns which wallet staked each of the blocks."""
        n0 = self.nodes[0]
        stakers = []
        for _ in range(count):
            height = n0.getblockcount()
            self.t += SLOT
            self.set_time(self.t)
            wait_until(lambda: n0.getblockcount() > height, timeout=60)
            # one slot, one block: the wallet that was second finds its block stale
            assert_equal(n0.getblockcount(), height + 1)
            stakers.append(self.staker_of(n0.getbestblockhash()))
        sync_blocks(self.nodes)
        return stakers

    def restart_node0(self, extra):
        # (at the same mocktime, or the node stakes a block right away)
        self.start_node(0, self.pos_args + extra + ["-wallet=wallet.dat", "-wallet=second", "-mocktime=%d" % self.t])
        connect_nodes_bi(self.nodes, 0, 1)
        n0 = self.nodes[0]
        w1, w2 = n0.get_wallet_rpc("wallet.dat"), n0.get_wallet_rpc("second")
        self.wallets = {"first": w1, "second": w2}
        return n0, w1, w2

    def run_test(self):
        n0 = self.nodes[0]
        self.t = n0.getblock(n0.getbestblockhash())["time"] + 600
        self.t -= self.t % SLOT
        self.set_time(self.t)

        self.log.info("Proof-of-work phase: two wallets in node0, each with coins on several addresses")
        n0.generate(30)
        first = n0.listwallets()[0]
        own_address = n0.getnewaddress()
        # (created while the node runs with -staking: it gets a staking thread)
        assert_equal(n0.createwallet("second")["name"], "second")
        w1, w2 = n0.get_wallet_rpc(first), n0.get_wallet_rpc("second")
        self.wallets = {"first": w1, "second": w2}
        # a staked coin rests for 60 blocks, and the coins of one address make one stake
        w1.sendmany("", {w1.getnewaddress(): 100000 for _ in range(COINS)})
        w1.sendmany("", {w2.getnewaddress(): 100000 for _ in range(COINS)})
        w1.generate(LAST_POW - 30)
        sync_blocks(self.nodes)
        assert_equal(n0.getblockcount(), LAST_POW)
        assert_equal(w2.getbalance(), COINS * 100000)
        # no more proof-of-work blocks after this height
        assert_raises_rpc_error(-1, None, w1.generate, 1)

        self.log.info("Both wallets stake: every block is the stake of one of them")
        # (the proof-of-work blocks ran ahead of the clock)
        self.t = n0.getblock(n0.getbestblockhash())["time"] + 2 * SLOT
        self.t -= self.t % SLOT
        stakers = self.stake(4)
        assert set(stakers) <= {"first", "second"}, stakers
        for wallet in (w1, w2):
            wait_until(lambda: wallet.getstakinginfo()["staking"], timeout=30)
            info = wallet.getstakinginfo()
            assert info["enabled"] and info["weight"] > 0 and info["search-interval"] > 0, info
        # the answer is that of the wallet asked: the weights are those of two different wallets
        assert w1.getstakinginfo()["weight"] != w2.getstakinginfo()["weight"]
        assert_equal(w1.getstakinginfo()["stakes_24h"] + w2.getstakinginfo()["stakes_24h"], 4)
        assert_equal(w1.getstakinginfo()["stakes_24h"], stakers.count("first"))

        self.log.info("An unloaded wallet stakes no more; the other one goes on")
        assert_equal(n0.unloadwallet("second")["warning"], "")
        del self.wallets["second"]
        assert_equal(self.stake(3), ["first"] * 3)

        self.log.info("A wallet that is loaded while the node runs has a staking thread again")
        assert_equal(n0.loadwallet("second")["name"], "second")
        self.wallets["second"] = w2
        self.stake(2)
        wait_until(lambda: w2.getstakinginfo()["staking"], timeout=30)

        self.log.info("The first wallet is encrypted and locked: the second one stakes alone")
        w1.encryptwallet(PASSPHRASE)
        n0.wait_until_stopped()
        n0, w1, w2 = self.restart_node0(["-staking=1"])
        assert_equal(self.stake(3), ["second"] * 3)
        assert not w1.getstakinginfo()["staking"]
        assert w2.getstakinginfo()["staking"]
        assert_equal(w1.getwalletinfo()["unlocked_until"], 0)

        self.log.info("Opened for staking only, the first wallet stakes; the second one is unloaded")
        n0.unloadwallet("second")
        del self.wallets["second"]
        w1.walletpassphrase(PASSPHRASE, 0, True)
        # (its staking thread looks every 10 seconds whether the wallet is still locked)
        assert_equal(self.stake(3), ["first"] * 3)
        assert_raises_rpc_error(None, "staking only", w1.signmessage, own_address, "probe")

        self.log.info("Loaded again, the second wallet stakes alone once the first one is locked")
        n0.loadwallet("second")
        self.wallets["second"] = w2
        w1.walletlock()
        assert_equal(self.stake(3), ["second"] * 3)

        self.log.info("Without -staking no wallet stakes, a wallet that is loaded later neither")
        self.stop_node(0)
        n0, w1, w2 = self.restart_node0([])
        w1.walletpassphrase(PASSPHRASE, 0, True)
        height = n0.getblockcount()
        n0.unloadwallet("second")
        n0.loadwallet("second")
        self.t += 2 * SLOT
        self.set_time(self.t)
        time.sleep(4)
        assert_equal(n0.getblockcount(), height)
        for wallet in (w1, w2):
            info = wallet.getstakinginfo()
            assert not info["enabled"] and not info["staking"], info
        assert_equal(self.nodes[1].getblockcount(), height)


if __name__ == '__main__':
    StakingMultiWalletTest().main()
