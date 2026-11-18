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
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from decimal import Decimal

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

    def params(self, archive=None, **changes):
        values = dict(self.hashes)
        values.update(changes)
        value = "-snapshotparams=%d:%s" % (values["height"], ":".join(values[k] for k in ("block", "utxo", "tokens", "governance", "index")))
        return value + (":" + archive if archive else "")

    def archive_sha256(self, folder, manifest):
        """SHA-256 of the snapshot files of the manifest, in order, as one stream."""
        sha = hashlib.sha256()
        for part in manifest["parts"]:
            with open(os.path.join(folder, part["file"]), "rb") as f:
                sha.update(f.read())
        return sha.hexdigest()

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
        self.other_fs = None
        try:
            self.run_cases()
        finally:
            if self.other_fs:
                # (the block directory of node2 is there: the node is stopped before it goes)
                try:
                    self.stop_nodes()
                finally:
                    shutil.rmtree(self.other_fs, ignore_errors=True)

    def run_cases(self):
        node0 = self.nodes[0]
        contrib = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "..", "contrib", "aokchain")

        self.log.info("A chain with transactions, a token and a governance command")
        # (every node of this test gets the master address: COMMON is what they are started with)
        master, frozen = node0.getnewaddress(), node0.getnewaddress()
        COMMON.append("-masteraddress=" + master)
        self.stop_node(0)
        self.start_node(0, COMMON)
        node0 = self.nodes[0]
        node0.generate(120)
        node0.issue("SNAPTOKEN", 1000)
        for i in range(20):
            node0.sendtoaddress(node0.getnewaddress(), 1 + i)
            if i == 3:
                node0.sendtoaddress(master, 10)
            if i == 4:
                # the master address freezes an address: the node that makes the snapshot keeps
                # a record of what that replaced, and the snapshot does not bring it along
                coin = node0.listunspent(1, 9999999, [master])[0]
                script = bytes.fromhex(node0.validateaddress(frozen)["scriptPubKey"])
                data = (bytes([71, 65, 70, len(script)]) + script).hex()
                raw = node0.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}], {"data": data, master: coin["amount"] - Decimal("0.01")})
                node0.sendrawtransaction(node0.signrawtransactionwithwallet(raw)["hex"])
            node0.generate(1)
        assert_equal(node0.checkfreeze(frozen), True)
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
        assert_equal(node1.checkfreeze(frozen), True)
        assert os.path.isdir(os.path.join(node1.datadir, "regtest", "governanceundo"))
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
            self.wipe(3)
            if field == "governance":
                # (the operator keeps one database elsewhere, behind a link: the removal
                # empties that place and leaves the link)
                kept_elsewhere = os.path.join(self.options.tmpdir, "tokens-of-node3")
                os.makedirs(kept_elsewhere)
                os.makedirs(os.path.join(self.nodes[3].datadir, "regtest"))
                os.symlink(kept_elsewhere, os.path.join(self.nodes[3].datadir, "regtest", "tokens"))
            self.start_node(3, COMMON + [source, self.params(**{field: other(self.hashes[field])})])
            self.assert_fell_back(3)
            assert "this program accepts %s" % other(self.hashes[field]) in self.log_of(3)
            if field == "governance":
                link = os.path.join(self.nodes[3].datadir, "regtest", "tokens")
                assert os.path.islink(link), "the link was replaced by a directory"
                assert [f for f in os.listdir(kept_elsewhere) if f.startswith("MANIFEST")], "the node does not keep its token database behind the link"
                assert "Snapshot: removed" in self.log_of(3)

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

        self.log.info("Files that are not the ones the program pins are not unpacked")
        # (the content would have the accepted hashes, but the program pins the published files)
        self.fresh(3, ["-bootstrapdir=" + self.archives, self.params(archive="00" * 32)])
        self.assert_fell_back(3, rejected=False)
        assert "Snapshot: the files are not the ones that this program accepts" in self.log_of(3)
        assert "Snapshot: accepted" not in self.log_of(3)

        self.log.info("The snapshot is accepted; the download survives a restart")
        self.wipe(2)
        node2 = self.nodes[2]
        # (with the files pinned, as on the main network)
        args2 = COMMON + STAKING + [source, self.params(archive=self.archive_sha256(self.archives, manifest))]
        server.delay = 1
        download = os.path.join(node2.datadir, "regtest", "snapshot-download")

        def downloaded():
            return sum(os.path.getsize(os.path.join(download, name)) for name in os.listdir(download)) if os.path.isdir(download) else 0

        node2.start(args2)
        wait_until(lambda: downloaded() > 0, timeout=60)
        node2.process.terminate()
        node2.process.wait(timeout=60)
        node2.running = False
        # ...and a kill of the program in the middle of the download: the start after it
        # takes the snapshot, the new node is not taken for one that an earlier version left
        # in an unknown state
        so_far = downloaded()
        node2.start(args2)
        wait_until(lambda: downloaded() > so_far, timeout=60)
        node2.process.kill()
        node2.process.wait(timeout=60)
        node2.running = False
        assert os.path.isfile(os.path.join(node2.datadir, "regtest", "aokchaind.pid"))
        node2.process = None
        assert "Snapshot: accepted" not in self.log_of(2)
        server.delay = 0
        del server.requests[:]
        self.start_node(2, args2)
        assert [r for r in server.requests if ".part" in r[0] and r[1]], "the download was not resumed"
        assert "Snapshot: accepted at height %d" % HEIGHT in self.log_of(2)
        assert "Snapshot: REJECTED" not in self.log_of(2)
        assert "was by an earlier version" not in self.log_of(2)
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
        # the governance state came with the snapshot; the records of what blocks replace in it
        # begin with the blocks that this node connects itself
        assert_equal(node2.checkfreeze(frozen), True)
        with open(os.path.join(node2.datadir, "regtest", "governanceundo.since")) as f:
            assert_equal(f.read().strip(), self.hashes["block"])

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

        self.log.info("A damaged database: the node starts over from the snapshot by itself and keeps its wallet")
        balance = node2.getbalance()
        assert balance > 0
        height2 = node2.getblockcount()
        state2 = node2.getstatehashes()
        self.stop_node(2)
        datadir2 = os.path.join(node2.datadir, "regtest")
        with open(os.path.join(datadir2, "governanceundo.since")) as f:
            since2 = f.read()
        tokens_aside = os.path.join(self.options.tmpdir, "tokens-of-node2")
        shutil.copytree(os.path.join(datadir2, "tokens"), tokens_aside)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        block_file = os.path.join(datadir2, "blocks", "blk00000.dat")
        block_file_size = os.path.getsize(block_file)

        def kept():
            """Nothing of the node's chain data was removed."""
            assert_equal(os.path.getsize(block_file), block_file_size)
            for name in ("chainstate", "governance", "governanceundo", os.path.join("blocks", "index")):
                assert [f for f in os.listdir(os.path.join(datadir2, name)) if f.startswith("MANIFEST")], name
            with open(os.path.join(datadir2, "governanceundo.since")) as f:
                assert_equal(f.read(), since2)
            assert not os.path.exists(os.path.join(datadir2, "snapshot-unverified"))

        def start_ends(args, message, code=1):
            """The start ends by itself with this exit code, and says this on its error output."""
            with tempfile.TemporaryFile("w+") as err:
                node2.start(args, stderr=err)
                assert_equal(node2.process.wait(timeout=120), code)
                node2.running = False
                node2.process = None
                err.seek(0)
                said = err.read()
            assert message in said, said
            return said

        # ...and the snapshot cannot be had: the node says why and ends, and it has removed
        # nothing. Whatever is in the way, for now or for good:
        del server.requests[:]
        without_source = [arg for arg in args2 if not arg.startswith("-bootstrapurl=")]
        for args, why in (
                ([arg if not arg.startswith("-bootstrapurl=") else "-bootstrapurl=http://127.0.0.1:9/" for arg in args2], "cannot be read from the address or folder"),
                (args2 + ["-snapshot=0"], "the snapshot is turned off"),
                (without_source, "no address or folder to take it from"),
                (without_source + ["-bootstrapdir=" + self.archives, self.params(archive=self.archive_sha256(self.archives, manifest), block="11" * 32)],
                 "this program accepts the one at height"),
                (without_source + ["-bootstrapdir=" + self.archives, self.params(archive="00" * 32)], "are not the ones that this program accepts")):
            start_ends(args, why)
            kept()
            assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
        starts_over = self.log_of(2).count("it starts over from the snapshot")
        assert starts_over >= 5
        assert_equal(self.log_of(2).count("Nothing was removed"), 5)
        # with its token database back, it is the node that it was: it has kept everything
        # (the starts that ended have begun an empty token database)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        shutil.copytree(tokens_aside, os.path.join(datadir2, "tokens"))
        self.start_node(2, args2)
        assert not [r for r in server.requests if ".part" in r[0]], "the snapshot was fetched"
        assert_equal(node2.getblockcount(), height2)
        assert_equal(node2.getbalance(), balance)
        assert_equal(node2.getstatehashes(), state2)
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over)

        # a stop that is asked for while the snapshot is fetched: the start ends without the
        # message of a failure, and nothing is removed
        self.stop_node(2)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        server.delay = 1
        del server.requests[:]
        with tempfile.TemporaryFile("w+") as err:
            node2.start(args2, stderr=err)
            wait_until(lambda: [r for r in server.requests if ".part" in r[0]], timeout=60)
            node2.process.terminate()
            node2.process.wait(timeout=120)
            node2.running = False
            node2.process = None
            err.seek(0)
            said = err.read()
        server.delay = 0
        assert "cannot be had now" not in said, said
        assert_equal(self.log_of(2).count("A stop was asked for while this node fetched the snapshot to start over from"), 1)
        kept()
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        shutil.copytree(tokens_aside, os.path.join(datadir2, "tokens"))
        self.start_node(2, args2)
        assert_equal(node2.getstatehashes(), state2)

        # a stop while it starts over: after the snapshot is unpacked beside the chain data...
        self.stop_node(2)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        node2.start(args2 + ["-testcrashat=snapshot-unpacked"])
        assert_equal(node2.process.wait(timeout=120), 0)
        node2.running = False
        node2.process = None
        kept()
        assert os.path.isdir(os.path.join(datadir2, "snapshot-unpack", "chainstate"))
        # ...and while it takes their place, at three starts one after the other: the next
        # start does the rest. (What the snapshot replaced is gone by then: it is not removed
        # for the starts that were stopped on it, however many.)
        removed = self.log_of(2).count("Snapshot: removed")
        for _ in range(3):
            node2.start(args2 + ["-testcrashat=snapshot-moving"])
            assert_equal(node2.process.wait(timeout=120), 0)
            node2.running = False
            node2.process = None
            assert os.path.exists(os.path.join(datadir2, "snapshot-unverified"))
            assert os.path.isdir(os.path.join(datadir2, "snapshot-unpack"))
        with open(os.path.join(datadir2, "snapshot-unverified"), "rb") as f:
            assert_equal(f.read(), b"3o")
        assert not os.path.exists(os.path.join(datadir2, "snapshot-unverified.new"))
        # (a marker without its content, as a version that wrote it in place could leave it
        # when it was stopped inside that write: the snapshot is kept all the same)
        open(os.path.join(datadir2, "snapshot-unverified"), "wb").close()
        del server.requests[:]
        self.start_node(2, args2)
        assert_equal(self.log_of(2).count("Snapshot: removed"), removed)
        assert not [r for r in server.requests if ".part" in r[0]], "the snapshot was fetched once more"
        assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
        assert "Snapshot: REJECTED" not in self.log_of(2)
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "missing")
        assert_equal(node2.getsnapshotinfo()["snapshot_height"], HEIGHT)
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        # the same when nothing gets in the way: one start, no message
        starts_over = self.log_of(2).count("it starts over from the snapshot")
        self.stop_node(2)
        shutil.rmtree(os.path.join(node2.datadir, "regtest", "governance"))
        del server.requests[:]
        self.start_node(2, args2)
        assert [r for r in server.requests if ".part" in r[0]], "the snapshot was not fetched again"
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over + 1)
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        # a stop right after the snapshot is accepted, before the marker of its check is gone:
        # the next start finds the marker beside the record of the acceptance, removes the
        # marker and checks nothing again
        self.stop_node(2)
        shutil.rmtree(os.path.join(datadir2, "governance"))
        removed = self.log_of(2).count("Snapshot: removed")
        rejected = self.log_of(2).count("Snapshot: REJECTED")
        node2.start(args2 + ["-testcrashat=snapshot-accepted"])
        assert_equal(node2.process.wait(timeout=120), 0)
        node2.running = False
        node2.process = None
        assert os.path.exists(os.path.join(datadir2, "snapshot-unverified"))
        assert os.path.exists(os.path.join(datadir2, "snapshot-without-past-blocks"))
        del server.requests[:]
        self.start_node(2, args2)
        assert not [r for r in server.requests if ".part" in r[0]], "the snapshot was fetched once more"
        assert_equal(self.log_of(2).count("this node accepted its snapshot"), 1)
        assert_equal(self.log_of(2).count("Snapshot: REJECTED"), rejected)
        assert_equal(self.log_of(2).count("Snapshot: removed"), removed)
        assert not os.path.exists(os.path.join(datadir2, "snapshot-unverified"))
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "missing")
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        starts_over = self.log_of(2).count("it starts over from the snapshot") - 1
        # A database that the operator keeps elsewhere (a link), and the block directory on
        # another file system: the snapshot goes where they are, and they stay where they are.
        self.stop_node(2)
        elsewhere = os.path.join(self.options.tmpdir, "elsewhere")
        os.makedirs(elsewhere)
        shutil.move(os.path.join(datadir2, "governance"), os.path.join(elsewhere, "governance"))
        os.symlink(os.path.join(elsewhere, "governance"), os.path.join(datadir2, "governance"))
        other_fs = None
        if os.path.isdir("/dev/shm") and os.access("/dev/shm", os.W_OK) and os.stat("/dev/shm").st_dev != os.stat(datadir2).st_dev:
            other_fs = tempfile.mkdtemp(prefix="aok-snapshot-test-", dir="/dev/shm")
            self.other_fs = other_fs
            shutil.move(os.path.join(datadir2, "blocks"), os.path.join(other_fs, "blocks"))
            os.symlink(os.path.join(other_fs, "blocks"), os.path.join(datadir2, "blocks"))
        else:
            self.log.info("(no second file system to be had here: the block directory stays where it is)")
        self.start_node(2, args2)
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.stop_node(2)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        del server.requests[:]
        self.start_node(2, args2)
        assert [r for r in server.requests if ".part" in r[0]], "the snapshot was not fetched again"
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over + 2)
        starts_over += 1
        assert os.path.islink(os.path.join(datadir2, "governance"))
        assert [f for f in os.listdir(os.path.join(elsewhere, "governance")) if f.startswith("MANIFEST")]
        if other_fs:
            assert os.path.islink(os.path.join(datadir2, "blocks"))
            assert [f for f in os.listdir(os.path.join(other_fs, "blocks", "index")) if f.startswith("MANIFEST")]
            assert_equal(self.log_of(2).count("blocks/index is not moved by a rename"), 0)   # (it is known before, and copied)
        assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        # a stop while an unpacked directory that was copied to its place is removed: under
        # its own name such a directory is always complete, so what is left of it is not
        # taken for one that is still to be copied
        self.stop_node(2)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        removed = self.log_of(2).count("Snapshot: removed")
        rejected = self.log_of(2).count("Snapshot: REJECTED")
        node2.start(args2 + ["-testcrashat=snapshot-copied"])
        assert_equal(node2.process.wait(timeout=120), 0)
        node2.running = False
        node2.process = None
        copied = []
        for where, dirs, files in os.walk(os.path.join(datadir2, "snapshot-unpack")):
            copied += [os.path.join(where, name) for name in dirs if name.endswith(".copied")]
        assert_equal(len(copied), 1)
        left = sorted(os.listdir(copied[0]))
        assert len(left) > 1
        for name in left[:len(left) // 2 + 1]:
            os.remove(os.path.join(copied[0], name))
        del server.requests[:]
        self.start_node(2, args2)
        assert not [r for r in server.requests if ".part" in r[0]], "the snapshot was fetched once more"
        assert_equal(self.log_of(2).count("Snapshot: REJECTED"), rejected)
        assert_equal(self.log_of(2).count("Snapshot: removed"), removed)
        assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
        assert os.path.islink(os.path.join(datadir2, "governance"))
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        # the directory that a file system keeps at its root, when the place of a database is
        # the mount point itself, is not the node's to remove, and is not in the way
        lost_found = os.path.join(elsewhere, "governance", "lost+found")
        os.mkdir(lost_found)
        open(os.path.join(lost_found, "file"), "w").close()
        os.chmod(lost_found, 0o555)
        self.stop_node(2)
        shutil.rmtree(os.path.join(datadir2, "tokens"))
        try:
            self.start_node(2, args2)
        finally:
            os.chmod(lost_found, 0o755)
        assert os.path.isfile(os.path.join(lost_found, "file"))
        shutil.rmtree(lost_found)
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        # ...and when the snapshot cannot be put into its place (the place of the governance
        # database cannot be written to): the start says what it has done, the unpacked
        # snapshot waits, and the start that can finish does so, however many ended before it
        # (the damage is in the block index database here, which a start opens first: it does
        # not get as far as the governance database before it starts over)
        self.stop_node(2)
        self.start_node(2, args2)   # (a start writes what the database has in its log into a table file)
        self.stop_node(2)
        index2 = os.path.join(datadir2, "blocks", "index")
        tables = sorted(name for name in os.listdir(index2) if name.endswith(".ldb"))
        assert tables
        os.remove(os.path.join(index2, tables[0]))
        # (first a place that cannot be written to at all: that is seen before anything is
        # removed, and the start-over is not begun)
        with open(os.path.join(datadir2, "governanceundo.since")) as f:
            since2 = f.read()
        block_file_size = os.path.getsize(block_file)
        for mode in (0o555, 0o333):   # (it cannot be written to; its content cannot be listed)
            os.chmod(os.path.join(elsewhere, "governance"), mode)
            try:
                said = start_ends(args2, "the place where this node keeps governance cannot be read or written to")
            finally:
                os.chmod(os.path.join(elsewhere, "governance"), 0o755)
            assert "Nothing was removed" in said, said
            kept()
            assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
        # (then one that can be written to and not emptied: something in it cannot be removed)
        stuck = os.path.join(elsewhere, "governance", "stuck")
        os.mkdir(stuck)
        open(os.path.join(stuck, "file"), "w").close()
        os.chmod(stuck, 0o555)
        nothing_removed = self.log_of(2).count("Nothing was removed")
        removed = self.log_of(2).count("Snapshot: removed")
        try:
            said = start_ends(args2, "it could not be put into the place of the chain data")
            assert "are removed" in said and "Nothing was removed" not in said, said
            for _ in range(3):
                start_ends(args2, "An earlier start had begun to put the chain state snapshot into the place of the chain data")
                assert os.path.isdir(os.path.join(datadir2, "snapshot-unpack", "governance"))
                assert os.path.exists(os.path.join(datadir2, "snapshot-unverified"))
        finally:
            os.chmod(stuck, 0o755)
        assert_equal(self.log_of(2).count("Nothing was removed"), nothing_removed)
        del server.requests[:]
        self.start_node(2, args2)
        assert not [r for r in server.requests if ".part" in r[0]], "the snapshot was fetched once more"
        assert_equal(self.log_of(2).count("Snapshot: removed"), removed)
        assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
        assert os.path.islink(os.path.join(datadir2, "governance"))
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        starts_over = self.log_of(2).count("it starts over from the snapshot") - 1   # (as the cases below count)
        # (a file system without room for the copy: nothing is begun. Only with a small file
        # system given by the caller, SNAPSHOT_TEST_SMALL_FS=<a directory on it>.)
        small_fs = os.environ.get("SNAPSHOT_TEST_SMALL_FS")
        if small_fs:
            self.stop_node(2)
            small = tempfile.mkdtemp(prefix="aok-snapshot-test-", dir=small_fs)
            os.rename(os.path.join(datadir2, "tokens"), os.path.join(self.options.tmpdir, "tokens-kept"))
            os.symlink(small, os.path.join(datadir2, "tokens"))
            with open(os.path.join(datadir2, "governanceundo.since")) as f:
                since2 = f.read()
            block_file = os.path.join(datadir2, "blocks", "blk00000.dat")
            block_file_size = os.path.getsize(block_file)
            said = start_ends(args2, "there is not enough room for tokens where this node keeps it")
            assert "Nothing was removed" in said, said
            kept()
            assert not os.path.exists(os.path.join(datadir2, "snapshot-unpack"))
            os.remove(os.path.join(datadir2, "tokens"))
            os.rename(os.path.join(self.options.tmpdir, "tokens-kept"), os.path.join(datadir2, "tokens"))
            shutil.rmtree(small)
            self.start_node(2, args2)
            connect_nodes(node2, 0)
            sync_blocks([node0, node2])
            self.assert_same_state(node0, node2, index=False, full=False)
            starts_over += 1
            self.log.info("(the case of the small file system ran)")
        # the block index database is damaged: indexing the block files again is no way out for
        # a node that does not have them, it starts over as well
        self.stop_node(2)
        self.start_node(2, args2)   # (a start writes what the database has in its log into a table file)
        self.stop_node(2)
        index2 = os.path.join(node2.datadir, "regtest", "blocks", "index")
        tables = sorted(name for name in os.listdir(index2) if name.endswith(".ldb"))
        assert tables
        os.remove(os.path.join(index2, tables[0]))
        del server.requests[:]
        self.start_node(2, args2)
        assert [r for r in server.requests if ".part" in r[0]], "the snapshot was not fetched again"
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over + 2)
        starts_over += 1
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        # ...and the same when the whole block index database is gone (the block from which on
        # the node keeps its governance records, the block of the snapshot, is gone with it)
        self.stop_node(2)
        shutil.rmtree(index2)
        del server.requests[:]
        self.start_node(2, args2)
        assert [r for r in server.requests if ".part" in r[0]], "the snapshot was not fetched again"
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over + 2)
        starts_over += 1
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        # A node without its chain state looks like a new one, and the snapshot is installed
        # as at a first start. It is the snapshot of a node that ran, though (its file says
        # so): starts that are stopped on it do not remove it.
        self.stop_node(2)
        shutil.rmtree(os.path.join(node2.datadir, "regtest", "chainstate"))
        del server.requests[:]
        removed = self.log_of(2).count("Snapshot: removed")
        for attempt in range(3):
            node2.start(args2 + ["-testcrashat=snapshot-moving"])
            assert_equal(node2.process.wait(timeout=120), 0)
            node2.running = False
            node2.process = None
            with open(os.path.join(node2.datadir, "regtest", "snapshot-unverified"), "rb") as f:
                assert_equal(f.read(), b"%do" % (attempt + 1))
        self.start_node(2, args2)
        assert_equal(self.log_of(2).count("Snapshot: removed"), removed)
        assert [r for r in server.requests if ".part" in r[0]], "the snapshot was not fetched again"
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over + 1)
        connect_nodes(node2, 0)
        sync_blocks([node0, node2])
        self.assert_same_state(node0, node2, index=False, full=False)
        assert_equal(node2.getbalance(), balance)
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "missing")

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
        # (a key of node0 that received coins before the snapshot)
        oldest = max(node0.listunspent(), key=lambda u: u["confirmations"])
        assert node0.getblockcount() - oldest["confirmations"] + 1 <= HEIGHT
        imported_address = oldest["address"]
        key = node0.dumpprivkey(imported_address)
        # a key is imported all the same; the wallet waits for the past blocks to look for its transactions
        assert_equal(node2.getsnapshotinfo()["wallet_waits"], False)
        assert "past blocks" in node2.importprivkey(key)
        assert_equal(node2.getsnapshotinfo()["wallet_waits"], True)
        assert_raises_rpc_error(-5, "Invalid or non-wallet transaction", node2.gettransaction, oldest["txid"])
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
        # (which coin wins a slot is not fixed: go on until one from before the snapshot has)
        for n in range(16):
            if old_coin_staked and n >= 4:
                break
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
        # the key that was imported without the past blocks: its transactions are there now
        wait_until(lambda: not node2.getsnapshotinfo()["wallet_waits"], timeout=60)
        node2.gettransaction(oldest["txid"])
        for height in (0, 1, 59, 60, 141, HEIGHT, HEIGHT + 1, LAST_POW + 3):
            assert_equal(node2.getblock(node2.getblockhash(height), 0), node0.getblock(node0.getblockhash(height), 0))
        assert_equal(node2.getrawtransaction(old_tx, True)["blockhash"], old_block)
        node2.rescanblockchain()
        assert os.path.exists(os.path.join(blocks_dir, "blk00000.dat"))

        self.log.info("With the past blocks it is a full node again: a new node synchronizes from it alone")
        self.stop_node(2)
        # (a stop right after the fetch had noted that the blocks are there left the file that
        # says they are not: the next start takes it away)
        without = os.path.join(node2.datadir, "regtest", "snapshot-without-past-blocks")
        assert not os.path.exists(without)
        open(without, "w").close()
        self.start_node(2, args2 + ["-mocktime=%d" % t])
        assert not os.path.exists(without)
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "present")
        # ...so that this node, which has all its blocks, builds its databases again from them
        # when it must, and does not start over from the snapshot
        starts_over2 = self.log_of(2).count("it starts over from the snapshot")
        height2 = node2.getblockcount()
        self.stop_node(2)
        open(without, "w").close()
        shutil.rmtree(os.path.join(node2.datadir, "regtest", "tokens"))
        del server.requests[:]
        self.start_node(2, args2 + ["-mocktime=%d" % t])
        wait_until(lambda: node2.getblockcount() == height2, timeout=240)
        assert not [r for r in server.requests if ".part" in r[0] and "snapshot" in r[0]], "the snapshot was fetched"
        assert_equal(self.log_of(2).count("it starts over from the snapshot"), starts_over2)
        assert "are built again from the block files at this start" in self.log_of(2)
        assert_equal(node2.getsnapshotinfo()["past_blocks"], "present")
        assert os.path.exists(os.path.join(blocks_dir, "blk00000.dat"))
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
        # (a first start that is stopped while the snapshot takes its place: the marker is
        # there, the record of an acceptance is not)
        self.wipe(3)
        self.nodes[3].start(COMMON + [source, self.params(), "-mocktime=%d" % t, "-testcrashat=snapshot-moving"])
        assert_equal(self.nodes[3].process.wait(timeout=120), 0)
        self.nodes[3].running = False
        self.nodes[3].process = None
        marker = os.path.join(self.nodes[3].datadir, "regtest", "snapshot-unverified")
        with open(marker, "rb") as f:
            assert_equal(f.read(), b"1")
        # ... and with other values in the program it is removed
        self.start_node(3, COMMON + [source, self.params(utxo=other(self.hashes["utxo"])), "-snapshot=0", "-mocktime=%d" % t])
        assert not os.path.exists(marker)
        assert "It is checked now" in self.log_of(3)
        assert "Snapshot: REJECTED" in self.log_of(3)
        assert_equal(self.nodes[3].getsnapshotinfo()["snapshot_height"], -1)


if __name__ == '__main__':
    SnapshotTest().main()
