#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the RPCs and options taken over from later Bitcoin Core versions.

scantxoutset, testmempoolaccept, the fee rate limit of sendrawtransaction,
getblockstats, getindexinfo, getnodeaddresses, getrpcinfo, getaddressinfo,
getbalances, signrawtransactionwithwallet, signrawtransactionwithkey,
-rpcwhitelist, -startupnotify, -shutdownnotify, -daemonwait.
"""

import base64
import hashlib
import hmac
import http.client
import json
import os
import subprocess
import time
from decimal import Decimal

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, rpc_port, wait_until


def rpcauth(user, password, salt="0123456789abcdef"):
    digest = hmac.new(salt.encode(), password.encode(), hashlib.sha256).hexdigest()
    return "-rpcauth=%s:%s$%s" % (user, salt, digest)


class UpstreamRpcTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], []]

    def call_as(self, node_index, user, password, body):
        conn = http.client.HTTPConnection("127.0.0.1", rpc_port(node_index), timeout=30)
        auth = base64.b64encode(("%s:%s" % (user, password)).encode()).decode()
        conn.request("POST", "/", json.dumps(body), {"Authorization": "Basic " + auth, "Content-Type": "application/json"})
        response = conn.getresponse()
        text = response.read().decode()
        conn.close()
        return response.status, text

    def run_test(self):
        node, other = self.nodes
        node.generate(30)
        self.sync_all()

        self.log.info("scantxoutset: the balance of an address without a wallet")
        key_address = other.getnewaddress()
        txid = node.sendtoaddress(key_address, 7)
        node.sendtoaddress(key_address, 5)
        node.generate(1)
        self.sync_all()
        scan = node.scantxoutset("start", [key_address])
        assert_equal(scan["success"], True)
        assert_equal(scan["height"], node.getblockcount())
        assert_equal(scan["bestblock"], node.getbestblockhash())
        assert_equal(scan["total_amount"], Decimal("12"))
        assert_equal(len(scan["unspents"]), 2)
        assert txid in [u["txid"] for u in scan["unspents"]]
        assert_equal(scan["txouts"], node.gettxoutsetinfo()["txouts"])
        # the other forms of a scan object, and several at once
        script = other.validateaddress(key_address)["scriptPubKey"]
        assert_equal(node.scantxoutset("start", ["addr(%s)" % key_address])["total_amount"], Decimal("12"))
        assert_equal(node.scantxoutset("start", [{"desc": "raw(%s)" % script}])["total_amount"], Decimal("12"))
        empty = node.getnewaddress()
        both = node.scantxoutset("start", [empty, key_address])
        assert_equal(both["total_amount"], Decimal("12"))
        assert_equal(node.scantxoutset("start", [empty])["unspents"], [])
        assert_raises_rpc_error(-5, "Invalid scan object 0", node.scantxoutset, "start", ["nothing"])
        assert_raises_rpc_error(-8, "only \"start\"", node.scantxoutset, "status", [key_address])
        # spent outputs are gone
        other.sendtoaddress(node.getnewaddress(), 11.5)
        self.sync_all()
        node.generate(1)
        self.sync_all()
        assert node.scantxoutset("start", [key_address])["total_amount"] < Decimal("1")

        self.log.info("testmempoolaccept and the fee rate limit of sendrawtransaction")
        utxo = [u for u in node.listunspent() if u["amount"] > 20][0]
        target = other.getnewaddress()

        def signed(fee):
            raw = node.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}], {target: utxo["amount"] - Decimal(fee)})
            result = node.signrawtransactionwithwallet(raw)
            assert_equal(result["complete"], True)
            return result["hex"]

        good = signed("0.01")
        pool = node.getrawmempool()
        result = node.testmempoolaccept([good])
        assert_equal(len(result), 1)
        assert_equal(result[0]["allowed"], True)
        assert_equal(result[0]["txid"], node.decoderawtransaction(good)["txid"])
        assert_equal(node.getrawmempool(), pool)  # nothing was added
        # the same again: a test leaves no trace
        assert_equal(node.testmempoolaccept([good])[0]["allowed"], True)
        # a fee rate limit: 0.01 for about 200 bytes is 0.05 per kB
        rejected = node.testmempoolaccept([good], "0.001")[0]
        assert_equal(rejected["allowed"], False)
        assert "absurdly-high-fee" in rejected["reject-reason"], rejected
        assert_equal(node.testmempoolaccept([good], "1.0")[0]["allowed"], True)
        assert_equal(node.testmempoolaccept([good], 0)[0]["allowed"], True)
        assert_raises_rpc_error(-26, "absurdly-high-fee", node.sendrawtransaction, good, "0.001")
        # an unsigned transaction and a spent input are refused with the reason
        unsigned = node.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}], {target: utxo["amount"] - Decimal("0.01")})
        assert_equal(node.testmempoolaccept([unsigned])[0]["allowed"], False)
        assert_raises_rpc_error(-8, "exactly one raw transaction", node.testmempoolaccept, [good, good])
        assert_raises_rpc_error(-22, "TX decode failed", node.testmempoolaccept, ["00"])
        # the old boolean still works
        huge = signed("12")
        assert_equal(node.testmempoolaccept([huge])[0]["allowed"], False)
        assert_equal(node.testmempoolaccept([huge], True)[0]["allowed"], True)
        sent = node.sendrawtransaction(good, "1.0")
        assert sent in node.getrawmempool()
        again = node.testmempoolaccept([good])[0]
        assert_equal(again["allowed"], False)
        assert "txn-already-in-mempool" in again["reject-reason"]
        assert_equal(node.testmempoolaccept([huge], True)[0]["allowed"], False)  # its input is spent in the mempool

        self.log.info("signrawtransactionwithkey")
        utxo2 = [u for u in node.listunspent() if u["amount"] > 2 and u["txid"] != utxo["txid"]][0]
        raw = node.createrawtransaction([{"txid": utxo2["txid"], "vout": utxo2["vout"]}], {target: utxo2["amount"] - Decimal("0.01")})
        assert_equal(node.signrawtransactionwithkey(raw, [other.dumpprivkey(target)])["complete"], False)
        with_key = node.signrawtransactionwithkey(raw, [node.dumpprivkey(utxo2["address"])])
        assert_equal(with_key["complete"], True)
        assert_equal(with_key["hex"], node.signrawtransactionwithwallet(raw)["hex"])
        assert_equal(with_key["hex"], node.signrawtransaction(raw)["hex"])

        self.log.info("getblockstats")
        block_hash = node.generate(1)[0]
        self.sync_all()
        stats = node.getblockstats(block_hash)
        block = node.getblock(block_hash, 2)
        assert_equal(stats["blockhash"], block_hash)
        assert_equal(stats["height"], block["height"])
        assert_equal(stats["txs"], len(block["tx"]))
        assert_equal(stats["txs"], 2)
        assert_equal(stats["totalfee"], 1000000)
        assert_equal(stats["maxfee"], 1000000)
        assert_equal(stats["minfee"], 1000000)
        assert_equal(stats["avgfee"], 1000000)
        assert_equal(stats["ins"], 1)
        assert_equal(stats["outs"], sum(len(tx["vout"]) for tx in block["tx"]))
        assert_equal(stats["total_size"], sum(tx["size"] for tx in block["tx"]))
        assert_equal(stats["proof_of_stake"], False)
        assert_equal(node.getblockstats(block["height"]), stats)
        assert_equal(node.getblockstats(block["height"], ["txs", "totalfee"]), {"txs": 2, "totalfee": 1000000})
        assert_equal(node.getblockstats(0)["txs"], 1)
        assert_raises_rpc_error(-8, "out of range", node.getblockstats, 100000)
        assert_raises_rpc_error(-8, "Invalid selected statistic", node.getblockstats, 1, ["nothing"])

        self.log.info("getindexinfo, getnodeaddresses, getrpcinfo")
        info = node.getindexinfo()
        assert_equal(info["txindex"], {"synced": True, "best_block_height": node.getblockcount()})
        assert "tokenindex" in info
        assert "addressindex" not in info
        assert_equal(list(node.getindexinfo("txindex").keys()), ["txindex"])
        assert_equal(node.getindexinfo("addressindex"), {})
        assert_equal(node.getnodeaddresses(), [])
        assert_equal(node.getnodeaddresses(0), [])
        assert_raises_rpc_error(-8, "out of range", node.getnodeaddresses, 2501)
        active = node.getrpcinfo()["active_commands"]
        assert_equal([c["method"] for c in active], ["getrpcinfo"])
        assert active[0]["duration"] >= 0

        self.log.info("getaddressinfo and getbalances")
        mine = node.getnewaddress()
        info = node.getaddressinfo(mine)
        assert_equal(info["address"], mine)
        assert_equal(info["ismine"], True)
        assert "isvalid" not in info
        assert_equal(info["scriptPubKey"], node.validateaddress(mine)["scriptPubKey"])
        assert_equal(info["pubkey"], node.validateaddress(mine)["pubkey"])
        assert_equal(node.getaddressinfo(key_address)["ismine"], False)
        assert_raises_rpc_error(-5, "Invalid address", node.getaddressinfo, "nothing")
        balances = node.getbalances()
        assert_equal(balances["mine"]["trusted"], node.getbalance())
        assert_equal(balances["mine"]["untrusted_pending"], node.getunconfirmedbalance())
        assert_equal(balances["mine"]["immature"], node.getwalletinfo()["immature_balance"])
        assert "watchonly" not in balances
        node.importaddress(key_address, "", False)
        assert "watchonly" in node.getbalances()

        self.log.info("-rpcwhitelist")
        self.stop_node(1)
        self.start_node(1, [rpcauth("reader", "pw1"), rpcauth("nobody", "pw2"), rpcauth("free", "pw3"),
                            "-rpcwhitelist=reader:getblockcount,getbestblockhash", "-rpcwhitelist=reader: getblockhash",
                            "-rpcwhitelist=nobody", "-rpcwhitelistdefault=0"])
        # the test framework's own user has no entry and may call everything
        assert_equal(other.getblockcount(), node.getblockcount())
        status, text = self.call_as(1, "reader", "pw1", {"id": 1, "method": "getblockcount", "params": []})
        assert_equal(status, 200)
        assert_equal(json.loads(text)["result"], node.getblockcount())
        assert_equal(self.call_as(1, "reader", "pw1", {"id": 1, "method": "getblockhash", "params": [1]})[0], 200)
        assert_equal(self.call_as(1, "reader", "pw1", {"id": 1, "method": "getbalance", "params": []})[0], 403)
        assert_equal(self.call_as(1, "reader", "pw1", {"id": 1, "method": "stop", "params": []})[0], 403)
        # a batch is refused as a whole when one of its methods is not allowed
        batch = [{"id": 1, "method": "getblockcount", "params": []}, {"id": 2, "method": "getbestblockhash", "params": []}]
        status, text = self.call_as(1, "reader", "pw1", batch)
        assert_equal(status, 200)
        assert_equal(len(json.loads(text)), 2)
        assert_equal(self.call_as(1, "reader", "pw1", batch + [{"id": 3, "method": "dumpwallet", "params": ["x"]}])[0], 403)
        assert_equal(self.call_as(1, "nobody", "pw2", {"id": 1, "method": "getblockcount", "params": []})[0], 403)
        assert_equal(self.call_as(1, "free", "pw3", {"id": 1, "method": "getbalance", "params": []})[0], 200)
        assert_equal(self.call_as(1, "reader", "wrong", {"id": 1, "method": "getblockcount", "params": []})[0], 401)
        # with the default on (it is, when a whitelist is given), a user without an entry may call nothing
        self.stop_node(1)
        self.start_node(1, [rpcauth("reader", "pw1"), rpcauth("free", "pw3"), "-rpcwhitelist=reader:getblockcount",
                            "-rpcwhitelist=__cookie__:getblockcount,stop"])  # (the user of the test framework)
        assert_equal(self.call_as(1, "reader", "pw1", {"id": 1, "method": "getblockcount", "params": []})[0], 200)
        assert_equal(self.call_as(1, "free", "pw3", {"id": 1, "method": "getblockcount", "params": []})[0], 403)
        assert_equal(other.getblockcount(), node.getblockcount())
        self.stop_node(1)

        self.log.info("-startupnotify and -shutdownnotify")
        started = os.path.join(self.options.tmpdir, "started")
        stopped = os.path.join(self.options.tmpdir, "stopped")
        self.start_node(1, ["-startupnotify=touch %s" % started, "-shutdownnotify=touch %s" % stopped])
        wait_until(lambda: os.path.exists(started), timeout=30)
        assert not os.path.exists(stopped)
        self.stop_node(1)
        assert os.path.exists(stopped)

        self.log.info("-daemonwait returns when the node answers")
        datadir = self.nodes[1].datadir
        binary = os.getenv("AOKCHAIND", "aokchaind")
        cli = [os.getenv("AOKCHAINCLI", "aokchain-cli"), "-datadir=" + datadir]
        begin = time.time()
        subprocess.check_call([binary, "-datadir=" + datadir, "-daemonwait"], stdout=subprocess.DEVNULL, timeout=120)
        self.log.info("  started in %.1f s" % (time.time() - begin))
        # no waiting and no retry: it answers at once
        assert_equal(int(subprocess.check_output(cli + ["getblockcount"]).decode()), node.getblockcount())
        subprocess.check_call(cli + ["stop"], stdout=subprocess.DEVNULL)
        wait_until(lambda: not os.path.exists(os.path.join(datadir, "regtest", "aokchaind.pid")), timeout=60)
        time.sleep(1)
        # a start that fails makes the command fail (here: the data directory is in use)
        self.start_node(1)
        failed = subprocess.run([binary, "-datadir=" + datadir, "-daemonwait"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
        assert failed.returncode != 0


if __name__ == '__main__':
    UpstreamRpcTest().main()
