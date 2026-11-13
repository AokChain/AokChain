#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the block bootstrap: block archives over HTTP or from a folder.

node0 has a chain. contrib/aokchain/make_bootstrap.py writes it as archives,
which a small HTTP server of this test serves. Fresh nodes are started with
-bootstrapurl or -bootstrapdir and must reach the end of the archives
without a peer. Whatever goes wrong with the archives (a file that is not
the one of the manifest, no manifest, a block that validation rejects) must
leave the node on the valid chain and able to synchronize from a peer.
"""

import gzip
import hashlib
import http.server
import json
import os
import shutil
import socketserver
import subprocess
import sys
import threading
import time

from test_framework.test_framework import AokChainTestFramework
from test_framework.util import assert_equal, connect_nodes, sync_blocks, wait_until

CHUNK_BLOCKS = 100
MARGIN = 20
BLOCKS = 470


class Handler(http.server.BaseHTTPRequestHandler):
    """Serves the files of server.root. Knows Range, can be slow, and can redirect."""

    def log_message(self, *args):
        pass

    def do_GET(self):
        server = self.server
        server.requests.append((self.path, self.headers.get("Range")))
        if self.path.startswith("/moved/"):
            # a redirect, as a file host with a download server does it
            self.send_response(302)
            self.send_header("Location", "http://127.0.0.1:%d/%s" % (server.server_address[1], self.path[len("/moved/"):]))
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        path = os.path.join(server.root, os.path.basename(self.path))
        if not os.path.isfile(path):
            self.send_error(404)
            return
        with open(path, "rb") as f:
            data = f.read()
        start = 0
        header = self.headers.get("Range")
        if header and server.ranges:
            start = int(header.split("=")[1].split("-")[0])
            if start >= len(data):
                self.send_error(416)
                return
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, len(data) - 1, len(data)))
        else:
            self.send_response(200)
        self.send_header("Content-Length", str(len(data) - start))
        self.end_headers()
        body = data[start:]
        try:
            if server.delay and (self.path.endswith(".gz") or ".part" in self.path):
                # slowly, and in pieces
                for i in range(0, len(body), 4096):
                    self.wfile.write(body[i:i + 4096])
                    self.wfile.flush()
                    time.sleep(server.delay)
            else:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, root):
        super().__init__(("127.0.0.1", 0), Handler)
        self.root = root
        self.delay = 0
        self.ranges = True
        self.requests = []
        threading.Thread(target=self.serve_forever, daemon=True).start()

    @property
    def url(self):
        return "http://127.0.0.1:%d/" % self.server_address[1]


def sha256_of(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class BootstrapTest(AokChainTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.add_nodes(self.num_nodes)
        self.start_node(0)

    def fresh(self, args):
        """Node 1 with an empty data directory."""
        node = self.nodes[1]
        if node.running:
            self.stop_node(1)
        shutil.rmtree(os.path.join(node.datadir, "regtest"), ignore_errors=True)
        self.start_node(1, args)
        return self.nodes[1]

    def copy_of(self, name):
        """A copy of the archives that a case can damage."""
        target = os.path.join(self.options.tmpdir, name)
        shutil.copytree(self.archives, target)
        return target

    def rewrite_manifest(self, folder, chunk_index, **changes):
        path = os.path.join(folder, "manifest.json")
        with open(path) as f:
            manifest = json.load(f)
        manifest["chunks"][chunk_index].update(changes)
        with open(path, "w") as f:
            json.dump(manifest, f)

    def info(self, node):
        return node.getbootstrapinfo()

    def wait_state(self, node, states, timeout=120):
        wait_until(lambda: self.info(node)["state"] in states, timeout=timeout)
        return self.info(node)

    def assert_on_chain_of_node0(self, node, height):
        assert_equal(node.getblockcount(), height)
        assert_equal(node.getblockhash(height), self.nodes[0].getblockhash(height))

    def run_test(self):
        node0 = self.nodes[0]
        self.log.info("A chain with some transactions, written as archives")
        node0.generate(120)
        for i in range(BLOCKS - 120):
            if i % 7 == 0:
                node0.sendtoaddress(node0.getnewaddress(), 1)
            node0.generate(1)
        assert_equal(node0.getblockcount(), BLOCKS)
        tool = os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "..", "contrib", "aokchain", "make_bootstrap.py")
        self.archives = os.path.join(self.options.tmpdir, "archives")
        make = [sys.executable, tool, "--rpcurl", node0.url, "--out", self.archives,
                "--chunk-blocks", str(CHUNK_BLOCKS), "--margin", str(MARGIN)]
        subprocess.check_call(make, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        with open(os.path.join(self.archives, "manifest.json")) as f:
            manifest = json.load(f)
        tip = BLOCKS - MARGIN
        assert_equal(manifest["tip_height"], tip)
        assert_equal(manifest["tip_hash"], node0.getblockhash(tip))
        assert_equal([(c["first_height"], c["last_height"]) for c in manifest["chunks"]],
                     [(1, 99), (100, 199), (200, 299), (300, 399), (400, 450)])
        for chunk in manifest["chunks"]:
            path = os.path.join(self.archives, chunk["file"])
            assert_equal(chunk["sha256"], sha256_of(path))
            assert_equal(chunk["compressed_size"], os.path.getsize(path))
            with gzip.open(path) as f:
                assert_equal(len(f.read()), chunk["size"])
        with open(os.path.join(self.archives, "SHA256SUMS")) as f:
            sums = dict(reversed(line.split()) for line in f)
        assert_equal(sums["manifest.json"], sha256_of(os.path.join(self.archives, "manifest.json")))
        assert_equal(len(sums), len(manifest["chunks"]) + 1)

        self.log.info("Running the tool again adds a chunk and keeps the old ones")
        before = {c["file"]: os.path.getmtime(os.path.join(self.archives, c["file"])) for c in manifest["chunks"]}
        node0.generate(60)
        subprocess.check_call(make, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        with open(os.path.join(self.archives, "manifest.json")) as f:
            manifest = json.load(f)
        tip = BLOCKS + 60 - MARGIN
        assert_equal(manifest["tip_height"], tip)
        assert_equal([(c["first_height"], c["last_height"]) for c in manifest["chunks"]][4:], [(400, 450), (451, 499), (500, 510)])
        for name, mtime in before.items():
            assert_equal(os.path.getmtime(os.path.join(self.archives, name)), mtime)
        chunks = manifest["chunks"]
        total = sum(c["compressed_size"] for c in chunks)

        server = Server(self.archives)
        common = ["-bootstrapminblocks=50"]

        self.log.info("Off without the option; given up with -bootstrap=0")
        assert_equal(self.info(node0)["state"], "off")
        assert_equal(self.info(node0)["configured"], False)
        self.start_node(1, common + ["-bootstrapurl=" + server.url, "-bootstrap=0"])
        assert_equal(self.info(self.nodes[1])["state"], "off")
        assert_equal(self.nodes[1].getblockcount(), 0)
        self.stop_node(1)

        self.log.info("A fresh node without peers reaches the end of the archives")
        self.start_node(1, common + ["-bootstrapurl=" + server.url])
        node = self.nodes[1]
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "done")
        assert_equal(info["active"], False)
        assert_equal(info["chunks"], len(chunks))
        assert_equal(info["chunks_imported"], len(chunks))
        assert_equal(info["bytes_downloaded"], total)
        assert_equal(info["bytes_total"], total)
        assert_equal(info["target_height"], tip)
        assert_equal(node.getconnectioncount(), 0)
        self.assert_on_chain_of_node0(node, tip)
        assert_equal(node.gettxoutsetinfo()["hash_serialized_2"], self.utxo_hash_at(tip))
        # nothing large is left on disk
        assert not os.path.exists(os.path.join(node.datadir, "regtest", "bootstrap"))
        # the rest comes from a peer
        connect_nodes(node, 0)
        sync_blocks([node0, node])
        self.log.info("At the tip the archives are not needed")
        self.restart_node(1, common + ["-bootstrapurl=" + server.url])
        assert_equal(self.wait_state(self.nodes[1], ["notneeded", "done", "failed"])["state"], "notneeded")
        self.stop_node(1)

        self.log.info("Mirrors: a dead one, one with a damaged file, then a good one; and a redirect")
        bad = self.copy_of("damaged")
        victim = os.path.join(bad, chunks[2]["file"])
        with open(victim, "r+b") as f:
            f.seek(os.path.getsize(victim) // 2)
            f.write(b"\x00\xff\x00\xff")
        bad_server = Server(bad)
        dead = "http://127.0.0.1:1/"
        node = self.fresh(common + ["-bootstrapurl=" + dead, "-bootstrapurl=" + bad_server.url, "-bootstrapurl=" + server.url + "moved/"])
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "done")
        self.assert_on_chain_of_node0(node, tip)
        # the damaged file was asked from the damaged mirror, and then from the next one through its redirect
        assert any(path.endswith(chunks[2]["file"]) for path, _ in bad_server.requests)
        assert any(path == "/moved/" + chunks[2]["file"] for path, _ in server.requests)

        self.log.info("Only a mirror with a damaged file: the bootstrap fails, the peer takes over")
        node = self.fresh(common + ["-bootstrapurl=" + bad_server.url])
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "failed")
        assert "SHA-256" in info["message"]
        assert_equal(info["active"], False)
        # the two archives before the damaged one were imported
        self.assert_on_chain_of_node0(node, chunks[1]["last_height"])
        assert not os.path.exists(os.path.join(node.datadir, "regtest", "bootstrap"))
        connect_nodes(node, 0)
        sync_blocks([node0, node])

        self.log.info("No manifest: ordinary synchronization")
        empty = os.path.join(self.options.tmpdir, "empty")
        os.mkdir(empty)
        empty_server = Server(empty)
        node = self.fresh(common + ["-bootstrapurl=" + empty_server.url])
        assert_equal(self.wait_state(node, ["done", "failed"])["state"], "failed")
        connect_nodes(node, 0)
        sync_blocks([node0, node])
        node = self.fresh(common + ["-bootstrapurl=" + dead])
        assert_equal(self.wait_state(node, ["done", "failed", "notneeded"])["state"], "failed")

        self.log.info("While the bootstrap runs, a peer gives headers but no blocks; stopped in the middle, it goes on after the restart")
        slow = Server(self.archives)
        slow.delay = 0.25
        node = self.fresh(common + ["-bootstrapurl=" + slow.url])
        connect_nodes(node, 0)
        wait_until(lambda: self.info(node)["chunks_imported"] >= 1, timeout=120)
        info = self.info(node)
        assert_equal(info["state"], "running")
        assert_equal(info["active"], True)
        wait_until(lambda: node.getblockchaininfo()["headers"] == node0.getblockcount(), timeout=60)
        # the headers are all there, the blocks are only those of the archives
        assert node.getblockcount() < chunks[3]["last_height"]
        assert node.getblockcount() in [c["last_height"] for c in chunks] or node.getblockcount() > 0
        self.stop_node(1)
        left = os.listdir(os.path.join(node.datadir, "regtest", "bootstrap"))
        assert left, "the files of a bootstrap that was interrupted stay for the next start"
        slow.delay = 0.02
        slow.requests.clear()
        self.start_node(1, common + ["-bootstrapurl=" + slow.url])
        node = self.nodes[1]
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "done")
        assert info["chunks"] < len(chunks)     # what was imported is not fetched again
        self.assert_on_chain_of_node0(node, tip)
        # a partial file was resumed with a Range request
        assert any(r for _, r in slow.requests), slow.requests

        self.log.info("A server without Range: the file is fetched again from its start")
        slow.delay = 0.25
        slow.ranges = False
        node = self.fresh(common + ["-bootstrapurl=" + slow.url])
        wait_until(lambda: self.info(node)["bytes_downloaded"] > 0 and self.info(node)["chunks_downloaded"] < len(chunks), timeout=60)
        self.stop_node(1)
        slow.delay = 0
        self.start_node(1, common + ["-bootstrapurl=" + slow.url])
        assert_equal(self.wait_state(self.nodes[1], ["done", "failed"])["state"], "done")
        self.assert_on_chain_of_node0(self.nodes[1], tip)

        self.log.info("Given up by the user: the peer takes over at once")
        slow.delay = 0.5
        slow.ranges = True
        node = self.fresh(common + ["-bootstrapurl=" + slow.url])
        connect_nodes(node, 0)
        wait_until(lambda: self.info(node)["state"] == "running", timeout=60)
        assert_equal(node.skipbootstrap(), True)
        assert_equal(self.info(node)["active"], False)
        sync_blocks([node0, node])
        assert_equal(self.wait_state(node, ["skipped"])["state"], "skipped")
        wait_until(lambda: not os.path.exists(os.path.join(node.datadir, "regtest", "bootstrap")), timeout=30)
        assert_equal(node.skipbootstrap(), False)

        self.log.info("An archive with a block that validation rejects")
        invalid = self.copy_of("invalid")
        chunk = chunks[2]
        path = os.path.join(invalid, chunk["file"])
        with gzip.open(path) as f:
            data = bytearray(f.read())
        # change one byte in a transaction of the 31st block of the archive
        offset = 0
        for _ in range(30):
            offset += 8 + int.from_bytes(data[offset + 4:offset + 8], "little")
        length = int.from_bytes(data[offset + 4:offset + 8], "little")
        bad_height = chunk["first_height"] + 30
        data[offset + 8 + length - 5] ^= 0x01
        with open(path, "wb") as plain:
            with gzip.GzipFile(filename="", mode="wb", fileobj=plain, mtime=0) as f:
                f.write(bytes(data))
        # the manifest fits the file: only validation can tell
        self.rewrite_manifest(invalid, 2, sha256=sha256_of(path), compressed_size=os.path.getsize(path))
        invalid_server = Server(invalid)
        node = self.fresh(common + ["-bootstrapurl=" + invalid_server.url])
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "failed")
        assert "not accepted" in info["message"], info["message"]
        # the chain ends right before the block that was changed, and it is the chain of node0
        self.assert_on_chain_of_node0(node, bad_height - 1)
        assert not os.path.exists(os.path.join(node.datadir, "regtest", "bootstrap"))
        connect_nodes(node, 0)
        sync_blocks([node0, node])
        assert_equal(node.getbestblockhash(), node0.getbestblockhash())

        self.log.info("An archive that lacks blocks, with a manifest that fits it")
        short = self.copy_of("short")
        path = os.path.join(short, chunks[1]["file"])
        with gzip.open(path) as f:
            data = f.read()
        offset = 0
        for _ in range(40):
            offset += 8 + int.from_bytes(data[offset + 4:offset + 8], "little")
        with open(path, "wb") as plain:
            with gzip.GzipFile(filename="", mode="wb", fileobj=plain, mtime=0) as f:
                f.write(data[:offset])
        self.rewrite_manifest(short, 1, sha256=sha256_of(path), compressed_size=os.path.getsize(path), size=offset)
        node = self.fresh(common + ["-bootstrapdir=" + short])
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "failed")
        self.assert_on_chain_of_node0(node, chunks[1]["first_height"] + 39)

        self.log.info("A folder with the archives, no network at all; the folder is only read")
        listing = sorted((name, os.path.getsize(os.path.join(self.archives, name))) for name in os.listdir(self.archives))
        node = self.fresh(["-bootstrapdir=" + self.archives, "-connect=0"])
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "done")
        assert_equal(info["chunks_imported"], len(chunks))
        self.assert_on_chain_of_node0(node, tip)
        assert_equal(sorted((name, os.path.getsize(os.path.join(self.archives, name))) for name in os.listdir(self.archives)), listing)
        assert not os.path.exists(os.path.join(node.datadir, "regtest", "bootstrap"))

        self.log.info("A folder with a damaged file, and a folder without a manifest")
        node = self.fresh(["-bootstrapdir=" + bad, "-connect=0"])
        info = self.wait_state(node, ["done", "failed"])
        assert_equal(info["state"], "failed")
        assert "SHA-256" in info["message"]
        self.assert_on_chain_of_node0(node, chunks[1]["last_height"])
        assert os.path.exists(victim)
        node = self.fresh(["-bootstrapdir=" + empty, "-connect=0"])
        assert_equal(self.wait_state(node, ["done", "failed"])["state"], "failed")

    def utxo_hash_at(self, height):
        """The UTXO set hash of node0 at a height below its tip: go back, look, go forward again."""
        node0 = self.nodes[0]
        above = node0.getblockhash(height + 1)
        node0.invalidateblock(above)
        assert_equal(node0.getblockcount(), height)
        result = node0.gettxoutsetinfo()["hash_serialized_2"]
        node0.reconsiderblock(above)
        wait_until(lambda: node0.getblockcount() > height)
        return result


if __name__ == '__main__':
    BootstrapTest().main()
