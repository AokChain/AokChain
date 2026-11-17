#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Stop a node at arbitrary moments, many times, and require after every plain restart a node
that runs and has the state of its peer.

Node0 makes the traffic (payments, tokens, governance commands, blocks), node2 mines competing
branches, node1 is the node that is stopped: while blocks are connected, while it writes its
databases, while it shuts down, while it starts and repairs itself, while it rebuilds. It is
always started again without any option that an operator would have to choose, as a service
manager does. Then its five state hashes, its freeze list and its tokens must be those of
node0.

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

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, disconnect_nodes, sync_blocks

FREEZE, UNFREEZE = 70, 85
STATE = ("utxo", "utxo_legacy", "tokens", "governance", "index")


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
        self.num_nodes = 3
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
        if self.options.powerhelper:
            subprocess.check_call([self.options.powerhelper, "off"])
        try:
            node.process.kill()
        except Exception:
            pass
        node.process.wait()
        self.victim_gone()
        if self.options.powerhelper:
            subprocess.check_call([self.options.powerhelper, "on"])

    def start_victim(self, extra=(), kill_after=None):
        """Start node1. True when it runs; False when it ended by itself or was stopped on the way."""
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
        try:
            node.wait_for_rpc_connection()
        except AssertionError:
            code = node.process.wait()
            self.exits[code] = self.exits.get(code, 0) + 1
            self.victim_gone()
            return False
        return True

    def restart_victim(self):
        """What a service manager does: start, and start again when the node ended by itself."""
        for attempt in range(8):
            if self.start_victim():
                self.starts_needed[attempt + 1] = self.starts_needed.get(attempt + 1, 0) + 1
                return
        raise AssertionError("node1 does not come up")

    def victim_rpc(self, call):
        try:
            return call(self.nodes[1])
        except Exception:
            return None

    # ---- traffic

    def master_command(self, action, address):
        n0, n2 = self.nodes[0], self.nodes[2]
        coins = [u for u in n2.listunspent(0, 9999999, [self.master]) if u["amount"] > 1]
        if not coins:
            return False
        coin = self.rng.choice(coins)
        data = script_command(action, n0.validateaddress(address)["scriptPubKey"])
        raw = n2.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {"data": data, self.master: coin["amount"] - Decimal("0.01")})
        try:
            n2.sendrawtransaction(n2.signrawtransactionwithwallet(raw)["hex"])
        except Exception:
            return False
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
                if what < 0.25:
                    n0.sendtoaddress(rng.choice(self.victim_addresses), rng.randint(1, 50))
                elif what < 0.45:
                    name = rng.choice(self.tokens)
                    n0.transfer(name, rng.randint(1, 5), rng.choice(self.victim_addresses + self.pool))
                elif what < 0.55 and len(self.tokens) < 12:
                    name = "RND%d" % len(self.tokens)
                    n0.issue(name, 100000, n0.getnewaddress(), "", 2, True)
                    self.mine()
                    self.tokens.append(name)
                elif what < 0.65:
                    n0.reissue(rng.choice(self.tokens), rng.randint(1, 100), n0.getnewaddress())
                elif what < 0.85:
                    # a payment from an address, a block, and then (in a later block) its freeze
                    free = [a for a in self.pool if a not in frozen]
                    if free:
                        address = rng.choice(free)
                        if self.pay_from(address):
                            self.mine()
                            if self.master_command(FREEZE, address):
                                frozen.add(address)
                else:
                    if frozen:
                        address = rng.choice(sorted(frozen))
                        if self.master_command(UNFREEZE, address):
                            frozen.discard(address)
            except Exception as e:
                self.log.debug("traffic: %s" % e)
            if rng.random() < 0.7:
                self.sync_peers_mempool()
                self.mine(rng.randint(1, 2))
        if rng.random() < 0.3:
            # node1 pays too, when it runs
            self.victim_rpc(lambda n: n.sendtoaddress(n0.getnewaddress(), 1))
            self.victim_rpc(lambda n: n.transfer(rng.choice(self.tokens), 1, n0.getnewaddress()))

    def sync_peers_mempool(self):
        """Node0 learns what node2 (the commands) has sent."""
        n0, n2 = self.nodes[0], self.nodes[2]
        if self.split:
            return
        deadline = time.time() + 20
        while time.time() < deadline and set(n2.getrawmempool()) - set(n0.getrawmempool()):
            time.sleep(0.1)

    def reorganise(self):
        """Node2 mines a longer branch on its own; the others give up theirs for it."""
        n0, n2, rng = self.nodes[0], self.nodes[2], self.rng
        sync_blocks([n0, n2], timeout=120)
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
        sync_blocks([n0, n2], timeout=120)
        self.reorganisations += 1

    def there_and_back(self):
        """All nodes give up a branch for another one and then come back to it. Node1 connects
        blocks again that it had validated before: on a healthy node that is nothing special."""
        n0 = self.nodes[0]
        asked = self.victim_log().count("The databases of this node are not what they were")
        self.reorganise()
        sync_blocks(self.nodes, timeout=180)
        # node0 goes back to its own branch and makes it the longer one
        other = n0.getblockhash(self.fork_height + 1)
        n0.invalidateblock(other)
        self.mine(self.rng.randint(2, 3))
        n0.reconsiderblock(other)
        sync_blocks(self.nodes, timeout=180)
        assert self.nodes[1].process.poll() is None, "node1 ended"
        assert_equal(self.victim_log().count("The databases of this node are not what they were"), asked)

    # ---- the rounds

    def settle(self):
        """Everything that was sent is in a block, on every node."""
        n0 = self.nodes[0]
        for _ in range(6):
            sync_blocks(self.nodes, timeout=180)
            # (a transaction that came back from a block that lost is not announced again:
            # node0 gets from the others what it does not have, and mines it)
            known = set(n0.getrawmempool())
            for node in self.nodes[1:]:
                for txid in node.getrawmempool():
                    if txid not in known:
                        try:
                            n0.sendrawtransaction(node.getrawtransaction(txid))
                        except Exception:
                            pass
            if not n0.getrawmempool():
                break
            self.mine()
        sync_blocks(self.nodes, timeout=180)

    def compare(self, what):
        n0, n1 = self.nodes[0], self.nodes[1]
        self.settle()
        def same(ours, theirs, part):
            if ours != theirs:
                raise AssertionError("%s: %s of node1 is not that of node0: %s / %s" % (what, part, ours, theirs))

        hashes = [node.getstatehashes() for node in (n0, n1)]
        for part in STATE:
            same(hashes[1][part], hashes[0][part], "the state hash '%s'" % part)
        same(n1.freezelist(), n0.freezelist(), "the freeze list")
        same(sorted(n1.listtokens()), sorted(n0.listtokens()), "the list of tokens")
        for name in self.tokens:
            same(n1.gettokendata(name), n0.gettokendata(name), "token %s" % name)
        for address in self.pool + self.victim_addresses:
            same(n1.listtokenbalancesbyaddress(address), n0.listtokenbalancesbyaddress(address), "the token balance of %s" % address)
        # The address index and the index of unspent outputs by address (commands read them;
        # they are not part of the state hashes)
        for address in self.pool + self.victim_addresses:
            query = {"addresses": [address]}
            same(n1.getaddressbalance(query), n0.getaddressbalance(query), "the indexed balance of %s" % address)
            same(sorted(u["txid"] + ":%d" % u["outputIndex"] for u in n1.getaddressutxos(query)),
                 sorted(u["txid"] + ":%d" % u["outputIndex"] for u in n0.getaddressutxos(query)), "the indexed unspent outputs of %s" % address)
            same(n1.getaddresstxids(query), n0.getaddresstxids(query), "the indexed transactions of %s" % address)

    def run_test(self):
        seed = self.options.seed if self.options.seed is not None else int(time.time())
        self.rng = rng = random.Random(seed)
        self.log.info("seed %d, %d rounds%s" % (seed, self.options.rounds, ", forced stops of the machine" if self.options.powerhelper else ""))
        self.exits, self.starts_needed = {}, {}
        self.split = False
        self.reorganisations = 0
        kinds = {}
        n0, n1, n2 = self.nodes

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
        self.stop_nodes()
        index = ["-addressindex=1", "-spentindex=1", "-timestampindex=1"]
        common = ["-masteraddress=" + self.master]
        # (the memory pool of node1 is not kept over a stop: what it held comes again from the others)
        # (-regtest: also when the device has lost the configuration file, this is no node of the main network)
        self.victim_args = common + index + ["-regtest", "-tokencatchupstep=2", "-persistmempool=0"]
        self.stderr1 = open(os.path.join(self.options.tmpdir, "node1.stderr"), "w")
        self.now = int(time.time()) + 3600
        self.start_node(0, common + index + ["-reindex"])   # (an index that is turned on is built by -reindex)
        self.start_node(2, common)
        self.nodes[1].start(self.victim_args + ["-reindex"], stderr=self.stderr1)
        self.nodes[1].wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 1, 2)
        n0, n1, n2 = self.nodes
        self.tokens = ["RND0"]
        n0.issue("RND0", 100000, n0.getnewaddress(), "", 2, True)
        self.mine()
        n0.transfer("RND0", 100, self.victim_addresses[0])
        self.mine(2)
        self.compare("before the first stop")
        os.sync()

        rebuild_dirs = ["tokens", "governance", "chainstate"]
        for round_number in range(1, self.options.rounds + 1):
            kind = rng.choice(["kill", "kill", "kill", "flush", "flush", "shutdown", "start", "crash-chainstate", "crash-tokens",
                               "rebuild", "damage", "reorganise", "reorganise", "there-and-back", "there-and-back"])
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
                connect_nodes_bi(self.nodes, 0, 1)
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
            elif kind == "damage":
                # a database is lost: the node rebuilds by itself, and is stopped on that way
                shutil.rmtree(os.path.join(self.nodes[1].datadir, "regtest", rng.choice(rebuild_dirs)))
                self.start_victim(kill_after=rng.uniform(0.2, 2.5))

            self.traffic()   # (the chain goes on while node1 is down)
            self.mine()
            self.restart_victim()
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            self.compare("round %d (%s)" % (round_number, kind))

        text = self.victim_log()
        self.log.info("stops by kind: %s" % kinds)
        self.log.info("starts that a restart took: %s; exits of node1 by itself, by code: %s; reorganisations: %d" % (self.starts_needed, self.exits, self.reorganisations))
        self.log.info("node1's log: %d starts after a run that did not end normally, %d governance repairs, %d token repairs, %d replays of the chain state, %d rebuilds by itself (%d taken up at a later start), %d times a validated block did not connect, %d repairs of the address index" % (
            text.count("did not end with a normal shutdown"), text.count("they are undone and come back with their blocks"),
            text.count("Tokens: the token database is at height"), text.count("Replaying blocks\n"),
            text.count("are built again from the block files at this start"), text.count("was decided at an earlier run and is not done"),
            text.count("The databases of this node are not what they were"), text.count("change(s) of the address index; they are undone")))


if __name__ == '__main__':
    RandomStopsTest().main()
