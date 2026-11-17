#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that disconnecting a block restores the token state from before it.

Every kind of token transaction is mined in a block of its own. The block is
then disconnected and connected again on the node that sent the transaction,
and each time the data of the tokens, the holders of every token, the hashes
of the chain state and the memory pool are compared with what they were:
 - a new token with its owner token, a sub token, a unique token, a username
 - transfers to an address that holds the token, to one that does not, and
   one that empties an address
 - reissues: more tokens to a holder and to a new address; no new tokens
   (only the data of the token changes, or reissuing ends) to a holder and
   to a new address

The same blocks are then disconnected
 - by the block verification of a start (-checkblocks, -checklevel=4),
 - all at once,
 - in reorganizations between two nodes, after which a reissue and a
   username registration are mined again on the common chain,
 - and, when the environment variable AOKCHAIND_PREVIOUS names the daemon of
   an earlier release, on a data directory whose blocks that release
   connected.

Node 0 mines. Node 1 sends the transactions from coins that it was paid:
a transaction that spends a block reward leaves the memory pool when its
block is disconnected, and here the transactions have to come back.
"""

import os

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
    disconnect_nodes,
    sync_blocks,
    sync_mempools,
    wait_until,
)

IPFS_HASH = "Qma21aBaw7pZmt1nV5MZKuGGeamDiZY34UbErV949wv2pg"


class TokensUndoTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def setup_network(self):
        # Node 2 runs the daemon of an earlier release first, if one is given.
        self.previous = os.getenv("AOKCHAIND_PREVIOUS")
        self.add_nodes(self.num_nodes, binary=[None, None, self.previous])
        self.start_node(0)
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)

    def token_state(self, node):
        """Everything the node knows about tokens, and the hashes of its chain state."""
        tokens = node.listtokens("*", True, 100000)
        holders = {}
        for name in sorted(tokens):
            for holder_of in (name, name + "!"):
                try:
                    holders[holder_of] = node.listaddressesbytoken(holder_of)
                except JSONRPCException:
                    pass
        return {"hashes": node.getstatehashes(), "tokens": tokens, "holders": holders}

    def chain_part(self, state, issue_blocks=True):
        """A state without the hash of the token database, which also covers the reissues
        that wait in the memory pool. With issue_blocks=False also without the height and
        the block of each token's issue: an earlier release stored others for a token that
        was reissued twice between two saves of the database."""
        hashes = dict(state["hashes"])
        del hashes["tokens"]
        del hashes["snapshotparams"]
        tokens = state["tokens"]
        if not issue_blocks:
            tokens = {name: {key: value for key, value in data.items() if key not in ("block_height", "blockhash")}
                      for name, data in tokens.items()}
        return {"hashes": hashes, "tokens": tokens, "holders": state["holders"]}

    def check_undo(self, what, action):
        miner, node = self.nodes[0], self.nodes[1]
        self.log.info("Disconnect and reconnect a block with %s" % what)
        parent = node.getbestblockhash()
        assert_equal(node.getrawmempool(), [])
        action()
        pool = node.getrawmempool()
        assert_equal(len(pool), 1)
        sync_mempools([miner, node])
        # The state before the block, with the transaction waiting in the pool: that is the
        # state to come back to, since a disconnected transaction goes back to the pool.
        before = self.token_state(node)
        block = miner.generate(1)[0]
        sync_blocks([miner, node])
        assert_equal(node.getrawmempool(), [])
        after = self.token_state(node)
        self.states[after["hashes"]["height"]] = after

        node.invalidateblock(block)
        assert_equal(node.getbestblockhash(), parent)
        assert_equal(node.getrawmempool(), pool)
        assert_equal(self.token_state(node), before)

        node.reconsiderblock(block)
        wait_until(lambda: node.getbestblockhash() == block, timeout=30)
        assert_equal(node.getrawmempool(), [])
        assert_equal(self.token_state(node), after)

    def run_test(self):
        n0, n1, n2 = self.nodes
        n0.generate(30)
        for _ in range(8):
            n0.sendtoaddress(n1.getnewaddress(), 1000000)
        n0.generate(1)
        sync_blocks([n0, n1])
        self.states = {}

        holder = n1.getnewaddress()
        other = n1.getnewaddress()
        self.first_height = n1.getblockcount() + 1
        self.states[n1.getblockcount()] = self.token_state(n1)

        self.check_undo("a new token and its owner token",
                        lambda: n1.issue("UNDOTEST", 1000, holder, "", 2, True))
        self.check_undo("a sub token",
                        lambda: n1.issue("UNDOTEST/SUB", 50, n1.getnewaddress()))
        # (one unique token: of a transaction that issues several, the memory pool forgets
        # only one name when the transaction is mined, and does not take it back later)
        self.check_undo("a unique token",
                        lambda: n1.issueunique("UNDOTEST", ["one"]))
        self.check_undo("a username",
                        lambda: n1.registerusername("@UNDOUSER"))

        self.check_undo("a transfer to an address without the token",
                        lambda: n1.transfer("UNDOTEST", 100, other))
        self.check_undo("a transfer to an address that holds the token",
                        lambda: n1.transfer("UNDOTEST", 50, other))
        at_other = n1.listtokenbalancesbyaddress(other)["UNDOTEST"]
        self.check_undo("a transfer that empties an address",
                        lambda: n1.transferfromaddress(other, "UNDOTEST", at_other, n1.getnewaddress()))
        assert "UNDOTEST" not in n1.listtokenbalancesbyaddress(other)

        self.check_undo("a reissue of more tokens to a holder",
                        lambda: n1.reissue("UNDOTEST", 500, holder))
        self.check_undo("a reissue of more tokens to an address without the token",
                        lambda: n1.reissue("UNDOTEST", 500, n1.getnewaddress()))
        self.check_undo("a reissue of no tokens to a holder, with new units",
                        lambda: n1.reissue("UNDOTEST", 0, holder, "", True, 4))
        self.check_undo("a reissue of no tokens to an address without the token",
                        lambda: n1.reissue("UNDOTEST", 0, n1.getnewaddress()))
        self.check_undo("a reissue of no tokens to an address without the token, with new units and data",
                        lambda: n1.reissue("UNDOTEST", 0, n1.getnewaddress(), "", True, 6, IPFS_HASH))
        self.check_undo("a reissue of no tokens that ends reissuing",
                        lambda: n1.reissue("UNDOTEST", 0, n1.getnewaddress(), "", False))

        tip_height, tip = n1.getblockcount(), n1.getbestblockhash()
        final = self.token_state(n1)
        assert_equal(final["tokens"]["UNDOTEST"]["amount"], 2000)
        assert_equal(final["tokens"]["UNDOTEST"]["units"], 6)
        assert_equal(final["tokens"]["UNDOTEST"]["reissuable"], 0)
        assert_equal(sum(final["holders"]["UNDOTEST"].values()), 2000)
        assert_equal(self.token_state(n0), final)

        self.log.info("A start that verifies the last blocks by disconnecting and connecting them")
        self.restart_node(1, extra_args=["-checkblocks=30", "-checklevel=4"])
        assert_equal(n1.getbestblockhash(), tip)
        assert_equal(self.token_state(n1), final)
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("All of these blocks disconnected at once, and connected again")
        first = n1.getblockhash(self.first_height)
        n1.invalidateblock(first)
        assert_equal(n1.getblockcount(), self.first_height - 1)
        assert_equal(self.chain_part(self.token_state(n1)), self.chain_part(self.states[self.first_height - 1]))
        n1.reconsiderblock(first)
        wait_until(lambda: n1.getbestblockhash() == tip, timeout=60)
        wait_until(lambda: n1.getrawmempool() == [], timeout=30)
        assert_equal(self.token_state(n1), final)

        self.log.info("A reorganization across a block with a reissue of no tokens")
        n1.issue("UNDOFORK", 10, n1.getnewaddress())
        sync_mempools([n0, n1])
        n0.generate(1)
        sync_blocks([n0, n1])
        assert_equal(self.token_state(n1), self.token_state(n0))
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        reissue_txid = n1.reissue("UNDOFORK", 0, n1.getnewaddress(), "", True, 3)[0]
        n1.generate(1)
        assert_equal(n1.gettokendata("UNDOFORK")["units"], 3)
        n0.generate(2)
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks([n0, n1])
        assert_equal(n1.gettokendata("UNDOFORK")["units"], 0)
        assert_equal(self.chain_part(self.token_state(n1)), self.chain_part(self.token_state(n0)))
        # (the reissue went back to the pool of node 1; it is mined on the common chain)
        assert reissue_txid in n1.getrawmempool()
        n1.generate(1)
        sync_blocks([n0, n1])
        assert_equal(n0.gettokendata("UNDOFORK")["units"], 3)
        assert_equal(self.token_state(n1), self.token_state(n0))

        self.log.info("A reorganization across a block with a username, which the other node then mines")
        disconnect_nodes(n0, 1)
        disconnect_nodes(n1, 0)
        username_txid = n1.registerusername("@FORKUSER")[0]
        username_hex = n1.gettransaction(username_txid)["hex"]
        n1.generate(1)
        registered_to = n1.getusernameaddress("@FORKUSER")
        n0.generate(2)
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks([n0, n1])
        assert "@FORKUSER" not in n1.listtokens()
        assert username_txid in n1.getrawmempool()
        if username_txid not in n0.getrawmempool():
            n0.sendrawtransaction(username_hex)
        n0.generate(1)
        sync_blocks([n0, n1])
        assert_equal(n0.getusernameaddress("@FORKUSER"), registered_to)
        assert_equal(n1.getusernameaddress("@FORKUSER"), registered_to)
        assert_equal(self.token_state(n1), self.token_state(n0))

        if not self.previous:
            self.log.info("AOKCHAIND_PREVIOUS is not set: no check of blocks connected by an earlier release")
            return

        self.log.info("Blocks connected by %s, disconnected by this build" % self.previous)
        self.start_node(2)
        connect_nodes_bi(self.nodes, 0, 2)
        sync_blocks([n0, n2])
        end_height, end = n2.getblockcount(), n2.getbestblockhash()
        reference = self.token_state(n0)
        assert_equal(self.chain_part(self.token_state(n2), False), self.chain_part(reference, False))
        self.stop_node(2)
        tested = os.getenv("AOKCHAIND", "aokchaind")
        n2.binary = tested
        n2.args[0] = tested
        self.start_node(2)
        assert_equal(n2.getbestblockhash(), end)
        for height in range(tip_height, self.first_height - 1, -1):
            n2.invalidateblock(n2.getblockhash(height))
            assert_equal(n2.getblockcount(), height - 1)
            assert_equal(self.chain_part(self.token_state(n2), False), self.chain_part(self.states[height - 1], False))
        n2.reconsiderblock(n0.getblockhash(self.first_height))
        wait_until(lambda: n2.getbestblockhash() == end, timeout=60)
        wait_until(lambda: n2.getrawmempool() == [], timeout=30)
        assert_equal(n2.getblockcount(), end_height)
        assert_equal(self.token_state(n2), reference)


if __name__ == '__main__':
    TokensUndoTest().main()
