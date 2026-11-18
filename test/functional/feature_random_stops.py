#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Stop a node at arbitrary moments, many times, and require after every plain restart a node
that runs and has the state of its peer.

Node0 makes the traffic (payments, tokens, governance commands, blocks), node2 mines competing
branches, node1 is the node that is stopped: while blocks are connected, while it writes its
databases, while it shuts down, while it starts and repairs itself, while it rebuilds (killed,
or asked to stop as a service manager asks: then it must end by itself). It is
always started again without any option that an operator would have to choose, as a service
manager does. Then its five state hashes, its freeze list, its tokens and its address index
must be those of node0.

The commands include ones that change nothing and several for one address, and the nodes give
blocks up for other branches all the time. Two nodes that made the same mistake in giving a
block up would agree with each other, so node0 is compared as well, in every comparison, with
a node that never gave a block up: node3 has no peers. It is handed the blocks of node0's
chain one after the other when the nodes have settled on them, and only ever connects. At the
end node2 also builds its chain state again from its blocks, and must have the same.

The test waits for what it needs for as long as it takes (a busy machine is no failure): a
node that is slow to start is waited for while its process runs, a port of node1 that another
program has taken while node1 was down is waited for, and connections are made again when a
wait for the same block does not end.

--rounds=<n>        how many stops (default 12)
--seed=<n>          the random seed (default: the time; it is logged)
--powerhelper=<p>   a program that is called with "off" before a kill and "on" after it. With one
                    that makes the device of node1's data directory drop its writes, the kill is
                    a forced stop of the machine: what was not synced is lost.
--victimdir=<d>     where the data directory of node1 is to lie (on that device)
"""

import os
import random
import shutil
import subprocess
import threading
import time
from decimal import Decimal

from test_framework.mininode import CTransaction, CTxOut, FromHex, ToHex
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, disconnect_nodes, sync_blocks, wait_until

FREEZE, UNFREEZE = 70, 85
STATE = ("utxo", "utxo_legacy", "tokens", "governance", "index")
# What the test waits for at most, in seconds. Nothing depends on these being short: they only
# end a run that will not get anywhere.
LONG = 1800
PORT_TAKEN = ("Unable to bind to", "Unable to start HTTP server")


def script_command(action, script_hex):
    script = bytes.fromhex(script_hex)
    return (bytes([71, 65, action, len(script)]) + script).hex()


class RandomStopsTest(AokChainTestFramework):
    def add_options(self, parser):
        parser.add_option("--rounds", dest="rounds", type=int, default=12)
        parser.add_option("--seed", dest="seed", type=int, default=None)
        parser.add_option("--powerhelper", dest="powerhelper", default="")
        parser.add_option("--victimdir", dest="victimdir", default="")

    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True

    def setup_chain(self):
        super().setup_chain()
        if self.options.victimdir:
            # the data directory of node1 lies where the power helper can cut it off
            target = os.path.join(self.options.victimdir, "node1")
            shutil.rmtree(target, ignore_errors=True)
            source = os.path.join(self.options.tmpdir, "node1")
            shutil.copytree(source, target)
            shutil.rmtree(source)
            os.symlink(target, source)
            os.sync()  # (its configuration file must not be among what the first cut loses)

    # ---- node1, the node that is stopped

    def victim_log(self):
        with open(os.path.join(self.nodes[1].datadir, "regtest", "debug.log"), encoding="utf8", errors="replace") as log:
            return log.read()

    def victim_gone(self):
        node = self.nodes[1]
        node.running = False
        node.process = None
        node.rpc_connected = False
        node.rpc = None

    def kill_victim(self):
        """A kill, or with a power helper a forced stop of the machine."""
        node = self.nodes[1]
        if node.process is None:
            return
        # (a node that has ended before this kill, by a signal that is not a kill of the test:
        # it crashed, whatever the round was about)
        ended = node.process.poll()
        if ended is not None and ended < 0 and ended != -9:
            raise AssertionError("node1 was ended by signal %d" % -ended)
        if self.options.powerhelper:
            subprocess.check_call([self.options.powerhelper, "off"])
        try:
            node.process.kill()
        except Exception:
            pass
        code = node.process.wait()
        if code < 0 and code != -9:
            raise AssertionError("node1 was ended by signal %d" % -code)
        self.victim_gone()
        if self.options.powerhelper:
            subprocess.check_call([self.options.powerhelper, "on"])

    def stderr_since(self, mark):
        self.stderr1.flush()
        with open(self.stderr1.name, encoding="utf8", errors="replace") as f:
            f.seek(mark)
            return f.read()

    def wait_for_start(self, node):
        """Waits until the node answers. Returns None then, or its exit code when it ended.
        (A node that needs long, repairing itself on a busy machine, is not one that failed.)"""
        deadline = time.time() + LONG
        while True:
            try:
                node.wait_for_rpc_connection()
                return None
            except AssertionError:
                if node.process.poll() is not None:
                    return node.process.wait()
                if time.time() > deadline:
                    raise AssertionError("node%d runs and has not answered in %d seconds" % (node.index, LONG))

    def start_victim(self, extra=(), kill_after=None, stop_after=None):
        """Start node1. True when it runs; False when it ended by itself or was stopped on the way.
        kill_after: it is killed after so many seconds. stop_after: it is asked to stop then, as a
        service manager asks, and must end by itself."""
        node = self.nodes[1]
        # (what a forced stop leaves of the last run's cookie file is not this run's)
        try:
            os.remove(os.path.join(node.datadir, "regtest", ".cookie"))
        except OSError:
            pass
        node.start(self.victim_args + ["-mocktime=%d" % self.now] + list(extra), stderr=self.stderr1)
        if kill_after is not None:
            time.sleep(kill_after)
            self.kill_victim()
            return False
        if stop_after is not None:
            time.sleep(stop_after)
            node.process.terminate()
            try:
                code = node.process.wait(timeout=LONG)
            except subprocess.TimeoutExpired:
                raise AssertionError("node1 was asked to stop while it started and has not ended in %d seconds" % LONG)
            # (-15: the request came before the program had its handler for it)
            if code < 0 and code != -15:
                raise AssertionError("node1 was asked to stop while it started, and was ended by signal %d" % -code)
            self.stops_asked += 1
            self.victim_gone()
            return False
        code = self.wait_for_start(node)
        if code is None:
            return True
        if code < 0:
            raise AssertionError("node1 was ended by signal %d while it started" % -code)
        self.exits[code] = self.exits.get(code, 0) + 1
        self.last_exit = code
        self.victim_gone()
        return False

    def restart_victim(self):
        """What a service manager does: start, and start again when the node ended by itself."""
        attempt = 0
        deadline = time.time() + LONG
        while attempt < 8:
            self.stderr1.flush()
            mark = os.path.getsize(self.stderr1.name)
            if self.start_victim():
                self.starts_needed[attempt + 1] = self.starts_needed.get(attempt + 1, 0) + 1
                return
            if any(text in self.stderr_since(mark) for text in PORT_TAKEN) and time.time() < deadline:
                # Its ports were free while it was down, and another program on this machine
                # has taken one: that is not the node's doing, and it passes.
                self.exits[self.last_exit] -= 1
                if not self.exits[self.last_exit]:
                    del self.exits[self.last_exit]
                self.port_waits += 1
                time.sleep(5)
                continue
            attempt += 1
        raise AssertionError("node1 does not come up")

    def victim_rpc(self, call):
        try:
            return call(self.nodes[1])
        except Exception:
            return None

    # ---- traffic

    def master_command(self, action, address, then=None):
        """A command of the master address for this address; then: a second one in the same transaction."""
        n0, n2 = self.nodes[0], self.nodes[2]
        coins = [u for u in n2.listunspent(0, 9999999, [self.master]) if u["amount"] > 1]
        if not coins:
            return False
        coin = self.rng.choice(coins)
        script = n0.validateaddress(address)["scriptPubKey"]
        raw = n2.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {"data": script_command(action, script), self.master: coin["amount"] - Decimal("0.01")})
        if then is not None:
            tx = FromHex(CTransaction(), raw)
            data = bytes.fromhex(script_command(then, script))
            tx.vout.append(CTxOut(0, bytes([0x6a, len(data)]) + data))
            raw = ToHex(tx)
        # While node2 is on its own, some commands go to the other side only: they are in the
        # branch that is given up, and not in the one that wins.
        to = n0 if self.split and self.rng.random() < 0.5 else n2
        try:
            to.sendrawtransaction(n2.signrawtransactionwithwallet(raw)["hex"])
        except Exception:
            return False
        if to is n0:
            self.lost_commands += 1
        return True

    def pay_from(self, address):
        """A payment of node0 from this address: a block that a later freeze of it must not undo."""
        n0 = self.nodes[0]
        coins = [u for u in n0.listunspent(1, 9999999, [address]) if u["amount"] > 2]
        if not coins:
            return False
        coin = self.rng.choice(coins)
        raw = n0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {n0.getnewaddress(): 1, address: coin["amount"] - Decimal("1.01")})
        try:
            n0.sendrawtransaction(n0.signrawtransactionwithwallet(raw)["hex"])
        except Exception:
            return False
        return True

    def mine(self, n=1, node=None):
        """Blocks, 20 seconds of the nodes' clock apart (they are made faster than time passes)."""
        for _ in range(n):
            self.now += 20
            for peer in (self.nodes[0], self.nodes[2]):
                peer.setmocktime(self.now)
            self.victim_rpc(lambda victim: victim.setmocktime(self.now))
            (node or self.nodes[0]).generate(1)

    def traffic(self):
        n0, rng = self.nodes[0], self.rng
        frozen = {a for a, f in n0.freezelist()["addresses"].items() if f}
        for _ in range(rng.randint(1, 3)):
            what = rng.random()
            try:
                if what < 0.20:
                    n0.sendtoaddress(rng.choice(self.victim_addresses), rng.randint(1, 50))
                elif what < 0.35:
                    name = rng.choice(self.tokens)
                    n0.transfer(name, rng.randint(1, 5), rng.choice(self.victim_addresses + self.pool))
                elif what < 0.43 and len(self.tokens) < 12:
                    name = "RND%d" % len(self.tokens)
                    n0.issue(name, 100000, n0.getnewaddress(), "", 2, True)
                    self.mine()
                    self.tokens.append(name)
                elif what < 0.50:
                    n0.reissue(rng.choice(self.tokens), rng.randint(1, 100), n0.getnewaddress())
                elif what < 0.68:
                    # a payment from an address, a block, and then (in a later block) its freeze
                    free = [a for a in self.pool if a not in frozen]
                    if free:
                        address = rng.choice(free)
                        if self.pay_from(address):
                            self.mine()
                            if self.master_command(FREEZE, address):
                                frozen.add(address)
                elif what < 0.76:
                    if frozen:
                        address = rng.choice(sorted(frozen))
                        if self.master_command(UNFREEZE, address):
                            frozen.discard(address)
                else:
                    # Commands that change nothing, commands for an address that the freeze list
                    # does not know yet, and several commands for one address: in one
                    # transaction, in two, in either order
                    address = rng.choice(self.pool) if rng.random() < 0.5 else n0.getnewaddress()
                    first, second = rng.choice([(FREEZE, UNFREEZE), (UNFREEZE, FREEZE), (FREEZE, FREEZE), (UNFREEZE, UNFREEZE), (FREEZE, None), (UNFREEZE, None)])
                    if second is None:
                        self.master_command(first, address)
                    elif rng.random() < 0.5:
                        self.master_command(first, address, then=second)
                    else:
                        self.master_command(first, address)
                        self.master_command(second, address)
                    self.odd_commands += 1
            except Exception as e:
                self.log.debug("traffic: %s" % e)
            if rng.random() < 0.7:
                self.sync_peers_mempool()
                self.mine(rng.randint(1, 2))
        if rng.random() < 0.3:
            # node1 pays too, when it runs
            self.victim_rpc(lambda n: n.sendtoaddress(n0.getnewaddress(), 1))
            self.victim_rpc(lambda n: n.transfer(rng.choice(self.tokens), 1, n0.getnewaddress()))

    def link(self):
        """The connections between the nodes that run (in a split, node2 is on its own)."""
        for a, b in [(0, 1)] + ([] if self.split else [(0, 2), (1, 2)]):
            if not (self.nodes[a].rpc_connected and self.nodes[b].rpc_connected):
                continue
            try:
                if not any("testnode%d" % b in peer["subver"] for peer in self.nodes[a].getpeerinfo()):
                    connect_nodes_bi(self.nodes, a, b)
            except Exception:
                pass

    def sync(self, nodes=None):
        """These nodes (default: the three that have peers) are at the same block. Nothing is
        mined while this waits. A connection that did not come about is made again."""
        nodes = nodes or self.nodes[:3]
        deadline = time.time() + LONG
        while True:
            try:
                sync_blocks(nodes, timeout=30)
                return
            except AssertionError:
                if time.time() > deadline:
                    raise
            self.link()

    def sync_peers_mempool(self):
        """Node0 learns what node2 (the commands) has sent."""
        n0, n2 = self.nodes[0], self.nodes[2]
        if self.split:
            return
        deadline = time.time() + 20
        while time.time() < deadline and set(n2.getrawmempool()) - set(n0.getrawmempool()):
            time.sleep(0.1)

    def reorganise(self, check=True):
        """Node2 mines a longer branch on its own; the others give up theirs for it."""
        n0, n2, rng = self.nodes[0], self.nodes[2], self.rng
        self.sync([n0, n2])
        disconnect_nodes(n0, 2)
        disconnect_nodes(n2, 0)
        self.victim_rpc(lambda n: disconnect_nodes(n, 2))
        self.split = True
        self.fork_height = n2.getblockcount()
        self.traffic()
        self.mine(rng.randint(1, 3))
        # (node2's branch is the longer one, whatever node0 has mined)
        self.mine(max(1, n0.getblockcount() - n2.getblockcount() + 1), n2)
        self.split = False
        connect_nodes_bi(self.nodes, 0, 2)
        if self.nodes[1].rpc_connected:
            try:
                connect_nodes_bi(self.nodes, 1, 2)
            except Exception:
                pass
        self.sync([n0, n2])
        self.reorganisations += 1
        if check:
            # at once, before the commands of the block that was given up are mined again
            self.against_reference("after a reorganisation", governance_only=True)

    def there_and_back(self):
        """All nodes give up a branch for another one and then come back to it. Node1 connects
        blocks again that it had validated before: on a healthy node that is nothing special."""
        n0 = self.nodes[0]
        asked = self.victim_log().count("The databases of this node are not what they were")
        # (node3 is not handed the blocks of node2's branch: the nodes give them up again. Node2
        # mined that branch and never had the one that node0 gave up for it.)
        self.reorganise(check=False)
        self.same_state(n0, self.nodes[2], "node0", "node2, which mined the branch", "there", governance_only=True)
        self.sync()
        # node0 goes back to its own branch...
        other = n0.getblockhash(self.fork_height + 1)
        n0.invalidateblock(other)
        # (at once: the commands of the branch that it gave up are in its memory pool, and the
        # blocks that it mines next take them up again)
        self.against_reference("back", governance_only=True)
        # ...and makes it the longer one
        self.mine(self.rng.randint(2, 3))
        n0.reconsiderblock(other)
        self.sync()
        self.against_reference("there and back", governance_only=True)
        assert self.nodes[1].process.poll() is None, "node1 ended"
        assert_equal(self.victim_log().count("The databases of this node are not what they were"), asked)

    def feed_reference(self):
        """Node3 is handed the blocks that node0 has and it has not, one after the other. It
        has no peers: no block reaches it that it would have to give up again."""
        n0, n3 = self.nodes[0], self.nodes[3]
        height = n3.getblockcount()
        if n0.getblockhash(height) != n3.getbestblockhash():
            raise AssertionError("the nodes gave up a block that node3 was handed (height %d): the test is at fault" % height)
        n3.setmocktime(self.now)
        for h in range(height + 1, n0.getblockcount() + 1):
            result = n3.submitblock(n0.getblock(n0.getblockhash(h), False))
            if result is not None:
                raise AssertionError("node3 does not take the block at height %d: %s" % (h, result))
        assert_equal(n3.getbestblockhash(), n0.getbestblockhash())

    def against_reference(self, what, governance_only=False):
        """Node0, which gave blocks up, has the state of node3, which never did.
        governance_only: at once, before anything else is mined (the commands of a block that
        was given up wait in the memory pools, and mining them again can hide a wrong inverse)."""
        n0, n3 = self.nodes[0], self.nodes[3]
        self.feed_reference()
        self.same_state(n0, n3, "node0", "a node that never gave a block up", what, governance_only)
        self.reference_checks += 1

    def against_rebuilt(self, what):
        """Node2 builds its chain state again from its blocks: node0 has the state of that node."""
        n0, n2 = self.nodes[0], self.nodes[2]
        self.settle()
        self.stop_node(2)
        deadline = time.time() + LONG
        while True:
            self.stderr2.flush()
            mark = os.path.getsize(self.stderr2.name)
            n2.start(self.common + ["-reindex-chainstate", "-mocktime=%d" % self.now], stderr=self.stderr2)
            code = self.wait_for_start(n2)
            if code is None:
                break
            with open(self.stderr2.name, encoding="utf8", errors="replace") as f:
                f.seek(mark)
                said = f.read()
            # (a port of node2 that another program took while node2 was down)
            if not any(text in said for text in PORT_TAKEN) or time.time() > deadline:
                raise AssertionError("node2 ended with code %s when it was to build its chain state again: %s" % (code, said))
            n2.running, n2.process, n2.rpc_connected, n2.rpc = False, None, False, None
            time.sleep(5)
        self.link()
        self.sync([n0, n2])
        # (node2 keeps no address index)
        self.same_state(n0, n2, "node0", "a node that built its state from the blocks", what, indexes=False)
        self.rebuilt_checks += 1

    # ---- the rounds

    def settle(self):
        """Everything that was sent is in a block, on every node."""
        n0 = self.nodes[0]
        for _ in range(6):
            self.sync()
            # (a transaction that came back from a block that lost is not announced again:
            # node0 gets from the others what it does not have, and mines it)
            known = set(n0.getrawmempool())
            for node in self.nodes[1:3]:
                for txid in node.getrawmempool():
                    if txid not in known:
                        try:
                            n0.sendrawtransaction(node.getrawtransaction(txid))
                        except Exception:
                            pass
            if not n0.getrawmempool():
                break
            self.mine()
        self.sync()

    def same_state(self, ours, theirs, who, whom, what, governance_only=False, indexes=True):
        def same(a, b, part):
            if a != b:
                raise AssertionError("%s: %s of %s is not that of %s: %s / %s" % (what, part, who, whom, a, b))

        hashes = [node.getstatehashes() for node in (ours, theirs)]
        for part in (("governance",) if governance_only else STATE):
            same(hashes[0][part], hashes[1][part], "the state hash '%s'" % part)
        same(ours.freezelist(), theirs.freezelist(), "the freeze list")
        same(ours.issuanceinfo(), theirs.issuanceinfo(), "the issuing costs and the fee address")
        if governance_only:
            return
        same(sorted(ours.listtokens()), sorted(theirs.listtokens()), "the list of tokens")
        for name in self.tokens:
            same(ours.gettokendata(name), theirs.gettokendata(name), "token %s" % name)
        for address in self.pool + self.victim_addresses:
            same(ours.listtokenbalancesbyaddress(address), theirs.listtokenbalancesbyaddress(address), "the token balance of %s" % address)
        if not indexes:
            return
        # The address index and the index of unspent outputs by address (commands read them;
        # they are not part of the state hashes)
        for address in self.pool + self.victim_addresses:
            query = {"addresses": [address]}
            same(ours.getaddressbalance(query), theirs.getaddressbalance(query), "the indexed balance of %s" % address)
            same(sorted(u["txid"] + ":%d" % u["outputIndex"] for u in ours.getaddressutxos(query)),
                 sorted(u["txid"] + ":%d" % u["outputIndex"] for u in theirs.getaddressutxos(query)), "the indexed unspent outputs of %s" % address)
            same(ours.getaddresstxids(query), theirs.getaddresstxids(query), "the indexed transactions of %s" % address)

    def compare(self, what):
        """Node1 has the state of node0, and node0 that of the node that never gave a block up."""
        self.settle()
        self.same_state(self.nodes[1], self.nodes[0], "node1", "node0", what)
        self.against_reference(what)

    def run_test(self):
        seed = self.options.seed if self.options.seed is not None else int(time.time())
        self.rng = rng = random.Random(seed)
        self.log.info("seed %d, %d rounds%s" % (seed, self.options.rounds, ", forced stops of the machine" if self.options.powerhelper else ""))
        self.exits, self.starts_needed = {}, {}
        self.split = False
        self.odd_commands = self.lost_commands = self.rebuilt_checks = self.reference_checks = self.port_waits = self.stops_asked = 0
        self.reorganisations = 0
        self.last_exit = None
        kinds = {}
        n0, n1, n2, n3 = self.nodes
        for node in self.nodes:
            node.rpc_timeout = 600   # (a call may have to wait for a node that is busy with its databases)

        # coins: node0 has the traffic, node2 the master address, node1 some of everything
        n0.generate(40)
        self.sync_all()
        self.master = n2.getnewaddress()
        self.pool = [n0.getnewaddress() for _ in range(6)]
        self.victim_addresses = [n1.getnewaddress() for _ in range(4)]
        for _ in range(4):
            n0.sendmany("", {self.master: 20})
            n0.sendmany("", {address: 30 for address in self.pool})
            n0.sendmany("", {address: 200 for address in self.victim_addresses})
        n0.generate(1)
        self.sync_all()
        index = ["-addressindex=1", "-spentindex=1", "-timestampindex=1"]
        common = self.common = ["-masteraddress=" + self.master]
        # (the memory pool of node1 is not kept over a stop: what it held comes again from the others)
        # (-regtest: also when the device has lost the configuration file, this is no node of the main network)
        self.victim_args = common + index + ["-regtest", "-tokencatchupstep=2", "-persistmempool=0"]
        self.stderr1 = open(os.path.join(self.options.tmpdir, "node1.stderr"), "w")
        self.stderr2 = open(os.path.join(self.options.tmpdir, "node2.stderr"), "w")
        self.now = int(time.time()) + 3600
        height = n0.getblockcount()
        self.stop_nodes()
        self.start_node(0, common + index + ["-reindex"])   # (an index that is turned on is built by -reindex)
        self.start_node(2, common)
        # node3: no peers, ever (it is handed its blocks, see feed_reference)
        self.start_node(3, common + index + ["-reindex", "-connect=0", "-listen=0"])
        self.nodes[1].start(self.victim_args + ["-reindex"], stderr=self.stderr1)
        self.nodes[1].wait_for_rpc_connection()
        # (no block is made or handed over before the block files are indexed again)
        for node in (self.nodes[0], self.nodes[1], self.nodes[3]):
            def indexed(node=node):
                with open(os.path.join(node.datadir, "regtest", "debug.log"), encoding="utf8", errors="replace") as log:
                    return "Reindexing finished" in log.read() and node.getblockcount() == height
            wait_until(indexed, timeout=LONG)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 1, 2)
        n0, n1, n2, n3 = self.nodes
        self.tokens = ["RND0"]
        n0.issue("RND0", 100000, n0.getnewaddress(), "", 2, True)
        self.mine()
        n0.transfer("RND0", 100, self.victim_addresses[0])
        self.mine(2)
        self.compare("before the first stop")
        os.sync()

        rebuild_dirs = ["tokens", "governance", "chainstate", "governanceundo"]
        for round_number in range(1, self.options.rounds + 1):
            kind = rng.choice(["kill", "kill", "kill", "flush", "flush", "shutdown", "start", "crash-chainstate", "crash-tokens",
                               "rebuild", "damage", "damage-stop", "reorganise", "reorganise", "there-and-back", "there-and-back"])
            kinds[kind] = kinds.get(kind, 0) + 1
            self.log.info("round %d: %s" % (round_number, kind))

            if kind == "there-and-back":
                # no stop in this round: reorganisations alone must not look like damage
                self.there_and_back()
                self.compare("round %d (%s)" % (round_number, kind))
                continue

            if kind in ("crash-chainstate", "crash-tokens"):
                # the node stops itself inside its next write
                self.kill_victim() if self.options.powerhelper else self.stop_node(1)
                switch = ["-dbcrashratio=%d" % rng.randint(1, 4), "-dbbatchsize=%d" % rng.choice([1, 100, 400])] if kind == "crash-chainstate" \
                    else ["-dbcrashtokenwrite=%d" % rng.randint(1, 2)]
                if not self.start_victim(switch):
                    self.restart_victim()
                    continue
                self.link()
                self.traffic()
                self.mine()
                for _ in range(6):
                    if self.victim_rpc(lambda n: n.gettxoutsetinfo()) is None:
                        break
                    self.traffic()
                self.kill_victim()
            else:
                timer = None
                if kind in ("kill", "flush", "reorganise"):
                    timer = threading.Timer(rng.uniform(0, 2.5), lambda: self.nodes[1].process and self.nodes[1].process.kill())
                    timer.start()
                if kind == "reorganise":
                    self.reorganise()
                self.traffic()
                if kind == "flush":
                    time.sleep(rng.uniform(0, 0.3))
                    self.victim_rpc(lambda n: n.gettxoutsetinfo())
                    self.traffic()
                if kind == "shutdown":
                    # a normal stop that is killed on its way
                    self.victim_rpc(lambda n: n.stop())
                    time.sleep(rng.uniform(0, 0.4))
                if timer:
                    timer.join()
                self.kill_victim()

            if kind == "start":
                # stopped again while it starts and repairs itself
                for _ in range(rng.randint(1, 3)):
                    self.start_victim(kill_after=rng.uniform(0, 1.5))
            elif kind == "rebuild":
                # an operator's rebuild that is stopped on its way
                self.start_victim(["-reindex-chainstate"], kill_after=rng.uniform(0.2, 2.5))
            elif kind in ("damage", "damage-stop"):
                # a database is lost: the node rebuilds by itself, and is stopped on that way:
                # killed, or asked to stop as a service manager asks
                lost = os.path.join(self.nodes[1].datadir, "regtest", rng.choice(rebuild_dirs))
                if os.path.isdir(lost):   # (a program before the records of the governance commands has no such directory)
                    shutil.rmtree(lost)
                if kind == "damage":
                    self.start_victim(kill_after=rng.uniform(0.2, 2.5))
                else:
                    self.start_victim(stop_after=rng.uniform(0, 2.5))

            self.traffic()   # (the chain goes on while node1 is down)
            self.mine()
            self.restart_victim()
            self.link()
            self.compare("round %d (%s)" % (round_number, kind))

        self.against_rebuilt("at the end")

        text = self.victim_log()
        self.log.info("stops by kind: %s" % kinds)
        self.log.info("starts that a restart took: %s; exits of node1 by itself, by code: %s; reorganisations: %d; rounds with odd governance commands: %d; commands sent to the side that lost only: %d; comparisons with the node that never gave a block up: %d, with a rebuilt node: %d; stops asked for during a rebuild: %d; waits for a port that another program held: %d" % (
            self.starts_needed, self.exits, self.reorganisations, self.odd_commands, self.lost_commands, self.reference_checks, self.rebuilt_checks, self.stops_asked, self.port_waits))
        self.log.info("node1's log: %d starts after a run that did not end normally, %d governance repairs, %d token repairs, %d replays of the chain state, %d rebuilds by itself (%d taken up at a later start), %d times a validated block did not connect, %d repairs of the address index" % (
            text.count("did not end with a normal shutdown"), text.count("they are undone and come back with their blocks"),
            text.count("Tokens: the token database is at height"), text.count("Replaying blocks\n"),
            text.count("are built again from the block files at this start"), text.count("was decided at an earlier run and is not done"),
            text.count("The databases of this node are not what they were"), text.count("change(s) of the address index; they are undone")))


if __name__ == '__main__':
    RandomStopsTest().main()
