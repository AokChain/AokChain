#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the chain state snapshot.

node0 makes a chain with transactions and a token. node1 synchronizes it from
the block archives without a transaction index, as the maker of a snapshot
does, and its data directory is
packaged with contrib/aokchain/make_snapshot.py. Fresh nodes are then started
with the values of getstatehashes as the accepted snapshot (-snapshotparams):

 - a node whose program accepts other values (a height, or the hash of the
   unspent outputs, of the tokens, of the governance state or of the block
   index) removes the snapshot and validates the blocks instead;
 - so does a node that gets a damaged or altered snapshot;
 - a node that accepts the snapshot starts at its height without the blocks
   before it, follows the chain, reorganizes above the snapshot height,
   refuses a fork that begins below it, receives, sends and stakes;
 - whatever needs the past blocks is refused with a message until fetchblocks
   has stored them, also across a restart in the middle; after that the node
   serves the whole chain to a new node;
 - a restored wallet that needs the past blocks is scanned once they are there.
"""

import json
import os
import shutil
import struct
import subprocess
import sys
import zlib

from feature_bootstrap import Server, sha256_of
from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, disconnect_nodes, sync_blocks, wait_until

LAST_POW = 260
HEIGHT = 150
SLOT = 16
NODE_NETWORK = 1
COMMON = ["-lastpowblock=%d" % LAST_POW, "-bootstrapminblocks=10"]
STAKING = ["-staking=1", "-emergencystaking=1"]
NEEDS_PAST_BLOCKS = "This needs the past blocks"


class SnapshotTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True

    def setup_network(self):
        self.add_nodes(self.num_nodes)
        self.start_node(0, COMMON)

    def wipe(self, i):
        node = self.nodes[i]
        if node.running:
            self.stop_node(i)
        shutil.rmtree(os.path.join(node.datadir, "regtest"), ignore_errors=True)

    def fresh(self, i, args):
        """Node i with an empty data directory."""
        self.wipe(i)
        self.start_node(i, COMMON + args)
        return self.nodes[i]

    def log_of(self, i):
        with open(os.path.join(self.nodes[i].datadir, "regtest", "debug.log"), errors="replace") as f:
            return f.read()

    def params(self, **changes):
        values = dict(self.hashes)
        values.update(changes)
        return "-snapshotparams=%d:%s" % (values["height"], ":".join(values[k] for k in ("block", "utxo", "tokens", "governance", "index")))

    def assert_same_state(self, a, b, index=True, full=True):
        """full=False after a reorganization: the undo data of a block has neither the time nor the
        coinstake mark of a spent coin, so an ordinary node gives such coins back without them and
        its full hash of the unspent outputs differs from that of a node that never reorganized.
        (A snapshot node keeps both for the coins from before the snapshot.)"""
        ha, hb = a.getstatehashes(), b.getstatehashes()
        for key in ("height", "block", "utxo_legacy", "tokens", "governance", "coins") + (("index",) if index else ()) + (("utxo",) if full else ()):
            assert_equal(ha[key], hb[key])

    def assert_fell_back(self, i, rejected=True):
        """The node took no snapshot and validated the blocks of the archives."""
        node = self.nodes[i]
        wait_until(lambda: node.getbootstrapinfo()["state"] in ("done", "failed"), timeout=120)
        assert_equal(node.getbootstrapinfo()["state"], "done")
        assert_equal(node.getblockcount(), HEIGHT)
        info = node.getsnapshotinfo()
        assert_equal(info["snapshot_height"], -1)
        assert_equal(info["past_blocks"], "present")
        assert_equal("Snapshot: REJECTED" in self.log_of(i), rejected)
        assert "Snapshot: accepted" not in self.log_of(i)
        assert not os.path.exists(os.path.join(node.datadir, "regtest", "snapshot-unverified"))
        # the state that validating the blocks gives is the state of the snapshot
        got = node.getstatehashes()
        for key in ("height", "block", "utxo", "tokens", "governance", "index"):
            assert_equal(got[key], self.hashes[key])
        assert node.getnetworkinfo()["localservices"] and int(node.getnetworkinfo()["localservices"], 16) & NODE_NETWORK
        node.getblock(node.getblockhash(5))

    def repack(self, name, change):
        """A copy of the archives whose snapshot stream was changed, with a manifest that fits it."""
        folder = os.path.join(self.options.tmpdir, name)
        shutil.copytree(self.archives, folder)
        with open(os.path.join(folder, "snapshot.json")) as f:
            manifest = json.load(f)
        packed = b"".join(open(os.path.join(folder, part["file"]), "rb").read() for part in manifest["parts"])
        stream = change(bytearray(zlib.decompress(packed, 31)))
        packer = zlib.compressobj(6, zlib.DEFLATED, 31)
        packed = packer.compress(bytes(stream)) + packer.flush()
        for part in manifest["parts"][1:]:
            os.remove(os.path.join(folder, part["file"]))
        part = manifest["parts"][0]
        with open(os.path.join(folder, part["file"]), "wb") as f:
            f.write(packed)
        part["size"] = len(packed)
        part["sha256"] = sha256_of(os.path.join(folder, part["file"]))
        manifest["parts"] = [part]
        manifest["size"] = len(stream)
        with open(os.path.join(folder, "snapshot.json"), "w") as f:
            json.dump(manifest, f)
        return folder

    def run_test(self):
        node0 = self.nodes[0]
        contrib = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "..", "contrib", "aokchain")

        self.log.info("A chain with transactions and a token")
        node0.generate(120)
        node0.issue("SNAPTOKEN", 1000)
        for i in range(20):
            node0.sendtoaddress(node0.getnewaddress(), 1 + i)
            node0.generate(1)
        old_tx = node0.sendtoaddress(node0.getnewaddress(), 7)
        node0.generate(1)
        backup = os.path.join(self.options.tmpdir, "wallet-at-141.dat")
        node0.backupwallet(backup)
        later_tx = node0.sendtoaddress(node0.getnewaddress(), 3)
        node0.transfer("SNAPTOKEN", 10, node0.getnewaddress())
        node0.generate(HEIGHT - node0.getblockcount())
        assert_equal(node0.getblockcount(), HEIGHT)
        old_block = node0.getblockhash(141)

        self.log.info("The block archives, a node that is synchronized from them only, and its snapshot")
        self.archives = os.path.join(self.options.tmpdir, "archives")
        subprocess.check_call([sys.executable, os.path.join(contrib, "make_bootstrap.py"), "--rpcurl", node0.url, "--out", self.archives,
                               "--chunk-blocks", "60", "--margin", "0"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        self.start_node(1, COMMON + ["-txindex=0", "-bootstrapdir=" + self.archives])
        node1 = self.nodes[1]
        wait_until(lambda: node1.getbootstrapinfo()["state"] in ("done", "failed"), timeout=120)
        assert_equal(node1.getblockcount(), HEIGHT)
        self.hashes = node1.getstatehashes()
        assert_equal(self.hashes["height"], HEIGHT)
        assert_equal(self.hashes["block"], node0.getblockhash(HEIGHT))
        assert_equal(self.hashes["utxo_legacy"], node1.gettxoutsetinfo()["hash_serialized_2"])
        assert_equal(self.hashes["coins"], node1.gettxoutsetinfo()["txouts"])
        self.assert_same_state(node0, node1, index=False)
        assert_equal(node1.getsnapshotinfo()["accepted_height"], 0)
        assert_equal(node1.getsnapshotinfo()["snapshot_height"], -1)
        assert_equal(node1.fetchblocks(), False)
        self.stop_node(1)
        hashes_file = os.path.join(self.options.tmpdir, "hashes.json")
        with open(hashes_file, "w") as f:
            json.dump(self.hashes, f)
        output = subprocess.check_output([sys.executable, os.path.join(contrib, "make_snapshot.py"), "--datadir", node1.datadir,
                                          "--network", "regtest", "--hashes", hashes_file, "--out", self.archives,
                                          "--max-file-mb", "1"], stderr=subprocess.DEVNULL).decode()
        assert self.params()[len("-snapshotparams="):] in output, output
        assert 'snapshot.utxo = uint256S("%s");' % self.hashes["utxo"] in output, output
        with open(os.path.join(self.archives, "snapshot.json")) as f:
            manifest = json.load(f)
        assert_equal(manifest["height"], HEIGHT)
        assert_equal(manifest["block_hash"], self.hashes["block"])
        for part in manifest["parts"]:
            assert_equal(part["sha256"], sha256_of(os.path.join(self.archives, part["file"])))
        with open(os.path.join(self.archives, "SHA256SUMS")) as f:
            sums = dict(reversed(line.split()) for line in f)
        assert_equal(sums["snapshot.json"], sha256_of(os.path.join(self.archives, "snapshot.json")))
        assert_equal(sums["manifest.json"], sha256_of(os.path.join(self.archives, "manifest.json")))
        assert_equal(sums[manifest["parts"][0]["file"]], manifest["parts"][0]["sha256"])

        node0.generate(10)
        server = Server(self.archives)
        source = "-bootstrapurl=" + server.url

        self.log.info("No snapshot without the values in the program, and none with -snapshot=0")
        self.fresh(3, [source])
        self.assert_fell_back(3, rejected=False)
        assert not [r for r in server.requests if "snapshot" in r[0]]
        self.fresh(3, [source, self.params(), "-snapshot=0"])
        self.assert_fell_back(3, rejected=False)
        assert not [r for r in server.requests if "snapshot" in r[0]]

        self.log.info("A program that accepts another height does not fetch this snapshot")
        self.fresh(3, [source, self.params(height=HEIGHT - 1)])
        self.assert_fell_back(3, rejected=False)
        assert not [r for r in server.requests if ".part" in r[0]]

        def other(value):
            return value[:-1] + ("0" if value[-1] != "0" else "1")

        for field in ("utxo", "tokens", "governance", "index"):
            self.log.info("A snapshot whose %s hash is not the accepted one is removed" % field)
            self.fresh(3, [source, self.params(**{field: other(self.hashes[field])})])
            self.assert_fell_back(3)
            assert "this program accepts %s" % other(self.hashes[field]) in self.log_of(3)

        self.log.info("A snapshot with a changed byte in a database file is removed")

        def flip(stream):
            # in the middle of the largest file of the chainstate
            position, best = 0, (0, 0)
            while True:
                (length,) = struct.unpack_from("<H", stream, position)
                if length == 0:
                    break
                name = bytes(stream[position + 2:position + 2 + length]).decode()
                (size,) = struct.unpack_from("<Q", stream, position + 2 + length)
                start = position + 2 + length + 8
                if name.startswith("chainstate/") and size > best[1]:
                    best = (start, size)
                position = start + size
            stream[best[0] + best[1] // 2] ^= 0x01
            return stream

        self.fresh(3, ["-bootstrapdir=" + self.repack("flipped", flip), self.params()])
        self.assert_fell_back(3)

        self.log.info("A snapshot with a file for another place is not unpacked")

        def escape(stream):
            name = b"../evil.dat"
            return struct.pack("<H", len(name)) + name + struct.pack("<Q", 4) + b"evil" + bytes(stream)

        self.fresh(3, ["-bootstrapdir=" + self.repack("escape", escape), self.params()])
        self.assert_fell_back(3, rejected=False)
        assert "does not belong into it" in self.log_of(3)
        assert not os.path.exists(os.path.join(self.nodes[3].datadir, "evil.dat"))

        self.log.info("A damaged download: the file is not the one of the manifest")
        damaged = os.path.join(self.options.tmpdir, "damaged")
        shutil.copytree(self.archives, damaged)
        with open(os.path.join(damaged, manifest["parts"][0]["file"]), "r+b") as f:
            f.seek(100)
            f.write(b"\x00\xff\x00\xff")
        self.fresh(3, ["-bootstrapdir=" + damaged, self.params()])
        self.assert_fell_back(3, rejected=False)
        assert "Snapshot: download failed" in self.log_of(3)

        self.log.info("The snapshot is accepted; the download survives a restart")
        self.wipe(2)
        node2 = self.nodes[2]
        args2 = COMMON + STAKING + [source, self.params()]
        server.delay = 1
        node2.start(args2)
        download = os.path.join(node2.datadir, "regtest", "snapshot-download")
        wait_until(lambda: os.path.isdir(download) and any(os.path.getsize(os.path.join(download, name)) > 0 for name in os.listdir(download)), timeout=60)
        node2.process.terminate()
        node2.process.wait(timeout=60)
        node2.running = False
        node2.process = None
        assert "Snapshot: accepted" not in self.log_of(2)
        server.delay = 0
        del server.requests[:]
        self.start_node(2, args2)
        assert [r for r in server.requests if ".part" in r[0] and r[1]], "the download was not resumed"
        assert "Snapshot: accepted at height %d" % HEIGHT in self.log_of(2)
        assert_equal(node2.getblockcount(), HEIGHT)
        assert_equal(node2.getbestblockhash(), self.hashes["block"])
        info = node2.getsnapshotinfo()
        assert_equal(info["accepted_height"], HEIGHT)
        assert_equal(info["snapshot_height"], HEIGHT)
        assert_equal(info["past_blocks"], "missing")
        assert_equal(info["wallet_waits"], False)
        got = node2.getstatehashes()
        for key in ("height", "block", "utxo", "utxo_legacy", "tokens", "governance", "index", "coins"):
            assert_equal(got[key], self.hashes[key])
        assert_equal(int(node2.getnetworkinfo()["localservices"], 16) & NODE_NETWORK, 0)
        assert_equal(node2.listtokens("SNAPTOKEN*"), node0.listtokens("SNAPTOKEN*"))
        blocks_dir = os.path.join(node2.datadir, "regtest", "blocks")
        assert not os.path.exists(os.path.join(node2.datadir, "regtest", "snapshot-download"))

        self.log.info("It follows the chain from a peer and keeps the same state")
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        assert_equal(node2.getblockcount(), HEIGHT + 10)
        self.assert_same_state(node0, node2, index=False)

        self.log.info("It cannot rebuild its databases from blocks that it does not have")
        self.stop_node(2)
        self.assert_start_raises_init_error(2, args2 + ["-reindex"], "does not have the blocks before it")
        self.assert_start_raises_init_error(2, args2 + ["-reindex-chainstate"], "does not have the blocks before it")
        self.start_node(2, args2)
        assert_equal(node2.getblockcount(), HEIGHT + 10)
        connect_nodes(node2, 0)

        self.log.info("A reorganization above the snapshot height")
        node0.invalidateblock(node0.getblockhash(HEIGHT + 8))
        node0.generate(8)
        sync_blocks([node0, node2])
        assert_equal(node2.getblockcount(), HEIGHT + 15)
        assert_equal(node2.getbestblockhash(), node0.getbestblockhash())
        self.assert_same_state(node0, node2, index=False, full=False)

        self.log.info("Receive and send")
        addresses = [node2.getnewaddress() for _ in range(6)]
        for address in addresses:
            node0.sendtoaddress(address, 100000)
        node0.generate(1)
        sync_blocks([node0, node2])
        assert_equal(node2.getbalance(), 600000)
        back = node2.sendtoaddress(node0.getnewaddress(), 1000)
        wait_until(lambda: back in node0.getrawmempool(), timeout=30)
        node0.generate(1)
        sync_blocks([node0, node2])
        assert_equal(node2.gettransaction(back)["confirmations"], 1)
        assert_equal(node0.gettransaction(back)["confirmations"], 1)

        self.log.info("A restart: the node comes up without the past blocks")
        self.stop_node(2)
        self.start_node(2, args2)
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "missing")
        assert_equal(node2.getblockcount(), node0.getblockcount())
        connect_nodes(node2, 0)

        self.log.info("A fork that begins below the snapshot height is refused")
        self.start_node(1, COMMON + ["-txindex=0"])
        node1.invalidateblock(node1.getblockhash(HEIGHT))
        node1.generate(40)
        assert node1.getblockcount() > node2.getblockcount()
        connect_nodes(node1, 2)
        wait_until(lambda: "at or below the snapshot height" in self.log_of(2), timeout=60)
        assert_equal(node2.getbestblockhash(), node0.getbestblockhash())
        self.stop_node(1)
        node0.generate(1)
        sync_blocks([node0, node2])

        self.log.info("What needs the past blocks is refused with a message")
        assert_raises_rpc_error(-1, NEEDS_PAST_BLOCKS, node2.getblock, old_block)
        assert_raises_rpc_error(-5, NEEDS_PAST_BLOCKS, node2.getrawtransaction, old_tx, True)
        assert_raises_rpc_error(-1, NEEDS_PAST_BLOCKS, node2.rescanblockchain)
        key = node0.dumpprivkey(node0.getnewaddress())
        assert_raises_rpc_error(-4, NEEDS_PAST_BLOCKS, node2.importprivkey, key)
        assert_raises_rpc_error(-4, "fetchblocks", node2.importaddress, node0.getnewaddress())
        # what the node has works
        node2.getblock(node2.getblockhash(HEIGHT + 3))
        node2.getrawtransaction(back, True)
        node2.rescanblockchain(HEIGHT + 1)
        node2.importprivkey(key, "", False)
        assert_equal(node2.getblockheader(old_block)["height"], 141)

        self.log.info("Proof of stake: the node validates stakes of coins from before the snapshot, and stakes")
        node0.generate(LAST_POW - node0.getblockcount())
        sync_blocks([node0, node2])
        t = node0.getblock(node0.getbestblockhash())["time"] + 2 * SLOT
        t -= t % SLOT
        # node0 stakes
        self.stop_node(0)
        self.start_node(0, COMMON + STAKING + ["-mocktime=%d" % t])
        self.stop_node(2)
        self.start_node(2, COMMON + [source, self.params(), "-mocktime=%d" % t])
        connect_nodes(node2, 0)
        old_coin_staked = False
        for _ in range(4):
            t += SLOT
            height = node0.getblockcount()
            node0.setmocktime(t)
            node2.setmocktime(t)
            wait_until(lambda: node0.getblockcount() > height, timeout=60)
            sync_blocks([node0, node2])
            block = node0.getblock(node0.getbestblockhash(), 2)
            assert "proof-of-stake" in block["flags"]
            staked = node0.getrawtransaction(block["tx"][1]["vin"][0]["txid"], True)
            if node0.getblockheader(staked["blockhash"])["height"] <= HEIGHT:
                old_coin_staked = True
        assert old_coin_staked, "no coin from before the snapshot was staked"
        assert "WARNING: time of the staked transaction" not in self.log_of(2)
        assert "WARNING: time of the staked transaction" not in self.log_of(0)
        # node2 stakes
        self.stop_node(0)
        self.start_node(0, COMMON + ["-mocktime=%d" % t])
        self.stop_node(2)
        self.start_node(2, args2 + ["-mocktime=%d" % t])
        connect_nodes(node2, 0)
        for _ in range(3):
            t += SLOT
            height = node2.getblockcount()
            node0.setmocktime(t)
            node2.setmocktime(t)
            wait_until(lambda: node2.getblockcount() > height, timeout=60)
            sync_blocks([node0, node2])
            assert "proof-of-stake" in node0.getblock(node0.getbestblockhash())["flags"]
        assert node0.getblockcount() >= LAST_POW + 7
        self.assert_same_state(node0, node2, index=False, full=False)

        self.log.info("fetchblocks stores the past blocks; a restart in the middle loses nothing")
        server.delay = 0.05
        assert_equal(node2.fetchblocks(), True)
        wait_until(lambda: node2.getsnapshotinfo()["bytes_done"] > 0, timeout=60)
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "downloading")
        assert_raises_rpc_error(-1, "is running", node2.fetchblocks)
        self.stop_node(2)
        server.delay = 0
        self.start_node(2, args2 + ["-mocktime=%d" % t])
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "missing")
        assert_raises_rpc_error(-1, NEEDS_PAST_BLOCKS, node2.getblock, old_block)
        assert_equal(node2.getbestblockhash(), node0.getbestblockhash())
        assert_equal(node2.fetchblocks(), True)
        wait_until(lambda: node2.getsnapshotinfo()["past_blocks"] != "downloading", timeout=120)
        info = node2.getsnapshotinfo()
        assert_equal(info["error"], "")
        assert_equal(info["past_blocks"], "present")
        assert_equal(info["blocks_stored"], HEIGHT + 1)
        assert_equal(info["snapshot_height"], HEIGHT)
        assert_equal(node2.fetchblocks(), False)
        for height in (0, 1, 59, 60, 141, HEIGHT, HEIGHT + 1, LAST_POW + 3):
            assert_equal(node2.getblock(node2.getblockhash(height), 0), node0.getblock(node0.getblockhash(height), 0))
        assert_equal(node2.getrawtransaction(old_tx, True)["blockhash"], old_block)
        node2.rescanblockchain()
        assert os.path.exists(os.path.join(blocks_dir, "blk00000.dat"))

        self.log.info("With the past blocks it is a full node again: a new node synchronizes from it alone")
        self.stop_node(2)
        self.start_node(2, args2 + ["-mocktime=%d" % t])
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "present")
        assert int(node2.getnetworkinfo()["localservices"], 16) & NODE_NETWORK
        self.fresh(3, ["-mocktime=%d" % t])
        connect_nodes(self.nodes[3], 2)
        sync_blocks([node2, self.nodes[3]], timeout=120)
        self.assert_same_state(node2, self.nodes[3], index=False, full=False)
        connect_nodes(node2, 0)

        self.log.info("A restored wallet waits for the past blocks and is scanned when they are there")
        self.wipe(3)
        os.makedirs(os.path.join(self.nodes[3].datadir, "regtest"))
        shutil.copy(backup, os.path.join(self.nodes[3].datadir, "regtest", "wallet.dat"))
        self.start_node(3, COMMON + [source, self.params(), "-mocktime=%d" % t])
        node3 = self.nodes[3]
        assert_equal(node3.getsnapshotinfo()["wallet_waits"], True)
        assert_equal(node3.gettransaction(old_tx)["txid"], old_tx)
        assert_raises_rpc_error(-5, None, node3.gettransaction, later_tx)
        # across a restart
        self.stop_node(3)
        self.start_node(3, COMMON + [source, self.params(), "-mocktime=%d" % t])
        assert_equal(node3.getsnapshotinfo()["wallet_waits"], True)
        assert_raises_rpc_error(-5, None, node3.gettransaction, later_tx)
        assert_equal(node3.fetchblocks(), True)
        wait_until(lambda: not node3.getsnapshotinfo()["wallet_waits"] and node3.getsnapshotinfo()["past_blocks"] == "present", timeout=120)
        assert_equal(node3.gettransaction(later_tx)["blockhash"], node0.gettransaction(later_tx)["blockhash"])
        connect_nodes(node3, 0)
        sync_blocks([node0, node3])
        assert_equal(node3.gettransaction(later_tx)["confirmations"], node0.gettransaction(later_tx)["confirmations"])

        self.log.info("-fetchblocks=1 downloads the past blocks at the start")
        self.fresh(3, [source, self.params(), "-fetchblocks=1", "-mocktime=%d" % t])
        wait_until(lambda: self.nodes[3].getsnapshotinfo()["past_blocks"] == "present", timeout=120)
        assert_equal(self.nodes[3].getblock(old_block, 0), node0.getblock(old_block, 0))

        self.log.info("A snapshot that an earlier start left unchecked is checked at the next start")
        self.wipe(3)
        self.start_node(3, COMMON + [source, self.params(), "-mocktime=%d" % t])
        self.stop_node(3)
        marker = os.path.join(self.nodes[3].datadir, "regtest", "snapshot-unverified")
        with open(marker, "w") as f:
            f.write("1")
        # ... and with other values in the program it is removed
        self.start_node(3, COMMON + [source, self.params(utxo=other(self.hashes["utxo"])), "-snapshot=0", "-mocktime=%d" % t])
        assert not os.path.exists(marker)
        assert "It is checked now" in self.log_of(3)
        assert "Snapshot: REJECTED" in self.log_of(3)
        assert_equal(self.nodes[3].getsnapshotinfo()["snapshot_height"], -1)


if __name__ == '__main__':
    SnapshotTest().main()
