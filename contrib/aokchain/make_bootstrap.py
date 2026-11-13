#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Write the blocks of a synced node as bootstrap archives.

The active chain is read in height order through the RPC interface of a
running node (getblockhash, getblock <hash> 0) and written as chunks in the
bootstrap.dat format: for every block the network magic, the length of the
block (4 bytes, little endian) and the raw block. A chunk covers a range of
heights (250,000 at most, fewer when the file would get larger than 190 MB)
and is compressed with gzip. manifest.json lists the chunks with their
heights, block hashes, sizes and SHA-256; SHA256SUMS has the same hashes
for sha256sum -c.

The output directory is complete as it is: serve it over HTTP, copy it to
mirrors, hand it out on a disk, or make a torrent of the directory. Nodes
take it with -bootstrapurl=<address> or -bootstrapdir=<directory>.

A node that is started with -bootstrapurl=<where these files are served>
downloads the chunks and imports them. It validates every block as it does
for blocks from peers, so the host of the files does not have to be trusted.

Run it again later to add chunks for the new blocks: chunks that are already
listed in the manifest are never written again.

Examples:
  make_bootstrap.py --datadir ~/.aokchain --out /srv/www/bootstrap
  make_bootstrap.py --rpcurl http://user:pass@127.0.0.1:33440 --out out --chunk-blocks 100000
"""

import argparse
import base64
import gzip
import hashlib
import http.client
import json
import os
import struct
import sys
import time
import urllib.parse

FORMAT_VERSION = 1
# message start bytes and default RPC ports, as in src/chainparams.cpp
NETWORKS = {
    "main": {"magic": bytes([0x3b, 0xee, 0xe0, 0x02]), "rpcport": 33440, "subdir": ""},
    "test": {"magic": bytes([0x44, 0x4b, 0x52, 0x04]), "rpcport": 44550, "subdir": "testnet3"},
    "regtest": {"magic": bytes([0x43, 0x52, 0x4f, 0x57]), "rpcport": 44550, "subdir": "regtest"},
}
BATCH = 250


class Rpc:
    """JSON-RPC over one kept-alive connection, with batches."""

    def __init__(self, url):
        parts = urllib.parse.urlparse(url)
        self.host, self.port = parts.hostname, parts.port
        auth = "%s:%s" % (urllib.parse.unquote(parts.username or ""), urllib.parse.unquote(parts.password or ""))
        self.headers = {"Authorization": "Basic " + base64.b64encode(auth.encode()).decode(),
                        "Content-Type": "application/json"}
        self.conn = None

    def _post(self, body):
        for attempt in range(3):
            try:
                if self.conn is None:
                    self.conn = http.client.HTTPConnection(self.host, self.port, timeout=600)
                self.conn.request("POST", "/", body, self.headers)
                response = self.conn.getresponse()
                data = response.read()
                if response.status in (401, 403):
                    raise SystemExit("RPC: wrong user or password (HTTP %d)" % response.status)
                return json.loads(data)
            except (http.client.HTTPException, ConnectionError, OSError):
                self.conn = None
                if attempt == 2:
                    raise
                time.sleep(1)

    def call(self, method, *params):
        reply = self._post(json.dumps({"id": 0, "method": method, "params": list(params)}))
        if reply.get("error"):
            raise SystemExit("RPC %s: %s" % (method, reply["error"]))
        return reply["result"]

    def batch(self, method, params_list):
        calls = [{"id": i, "method": method, "params": list(p)} for i, p in enumerate(params_list)]
        replies = self._post(json.dumps(calls))
        replies.sort(key=lambda r: r["id"])
        for reply in replies:
            if reply.get("error"):
                raise SystemExit("RPC %s: %s" % (method, reply["error"]))
        return [reply["result"] for reply in replies]


def rpc_url_from_datadir(datadir, network, port):
    """User and password from the cookie file or from aokchain.conf of a data directory."""
    datadir = os.path.expanduser(datadir)
    conf = {}
    try:
        with open(os.path.join(datadir, "aokchain.conf")) as f:
            for line in f:
                line = line.split("#")[0].strip()
                if "=" in line:
                    key, value = line.split("=", 1)
                    conf[key.strip()] = value.strip()
    except OSError:
        pass
    if network is None:
        network = "regtest" if conf.get("regtest") == "1" else "test" if conf.get("testnet") == "1" else "main"
    port = port or int(conf.get("rpcport", NETWORKS[network]["rpcport"]))
    cookie = os.path.join(datadir, NETWORKS[network]["subdir"], ".cookie")
    if os.path.exists(cookie):
        with open(cookie) as f:
            user, password = f.read().strip().split(":", 1)
    elif "rpcuser" in conf and "rpcpassword" in conf:
        user, password = conf["rpcuser"], conf["rpcpassword"]
    else:
        raise SystemExit("no .cookie file and no rpcuser/rpcpassword in %s: is the node running?" % datadir)
    return "http://%s:%s@127.0.0.1:%d" % (urllib.parse.quote(user, safe=""), urllib.parse.quote(password, safe=""), port)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for piece in iter(lambda: f.read(1 << 20), b""):
            digest.update(piece)
    return digest.hexdigest()


def write_chunk(rpc, magic, out_dir, first, last, compress, level, max_bytes, progress):
    """Blocks from first on into one file: up to last, or until the file reaches max_bytes.
    Returns the manifest entry."""
    temp = os.path.join(out_dir, "blocks-%08d.tmp" % first)
    raw_size = 0
    first_hash = last_hash = None
    previous = None
    with open(temp, "wb") as plain:
        # mtime 0 and no file name in the gzip header: the same blocks give the same file
        out = gzip.GzipFile(filename="", mode="wb", compresslevel=level, fileobj=plain, mtime=0) if compress else plain
        for start in range(first, last + 1, BATCH):
            heights = range(start, min(start + BATCH, last + 1))
            hashes = rpc.batch("getblockhash", [[h] for h in heights])
            blocks = rpc.batch("getblock", [[h, 0] for h in hashes])
            for height, block_hash, block_hex in zip(heights, hashes, blocks):
                block = bytes.fromhex(block_hex)
                # the chain must not have changed below us while we read it
                if previous is not None and block[4:36][::-1].hex() != previous:
                    raise SystemExit("block %d does not follow block %d: the chain was reorganized while reading. Run again." % (height, height - 1))
                previous = block_hash
                out.write(magic + struct.pack("<I", len(block)) + block)
                raw_size += 8 + len(block)
            if first_hash is None:
                first_hash = hashes[0]
            last_hash = hashes[-1]
            if (heights[-1] + 1) % 10000 < BATCH or heights[-1] == last:
                progress(heights[-1])
            if max_bytes and plain.tell() >= max_bytes:
                # large enough for one file: the chunk ends here
                last = heights[-1]
                break
        if compress:
            out.close()
    name = "blocks-%08d-%08d.dat%s" % (first, last, ".gz" if compress else "")
    path = os.path.join(out_dir, name)
    os.replace(temp, path)
    return {
        "file": name,
        "first_height": first,
        "last_height": last,
        "first_hash": first_hash,
        "last_hash": last_hash,
        "size": raw_size,
        "compressed_size": os.path.getsize(path),
        "compression": "gzip" if compress else "none",
        "sha256": sha256_of(path),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", help="data directory of the running node (for the RPC cookie or rpcuser/rpcpassword)")
    parser.add_argument("--rpcurl", help="http://user:password@host:port of the node, instead of --datadir")
    parser.add_argument("--rpcport", type=int, help="RPC port, when it is not the default and not in aokchain.conf")
    parser.add_argument("--out", required=True, help="directory for the chunks and manifest.json")
    parser.add_argument("--chunk-blocks", type=int, default=250000, help="heights per chunk at most (default 250000)")
    parser.add_argument("--max-file-mb", type=int, default=190,
                        help="end a chunk early when its file reaches this size, so that every file fits "
                             "the limits of common file hosts (default 190, 0 for no limit)")
    parser.add_argument("--margin", type=int, default=1000, help="stop this many blocks below the tip (default 1000)")
    parser.add_argument("--no-compress", action="store_true", help="write plain .dat chunks instead of .dat.gz")
    parser.add_argument("--level", type=int, default=9, help="gzip level 1-9 (default 9)")
    args = parser.parse_args()
    if not args.datadir and not args.rpcurl:
        parser.error("give --datadir or --rpcurl")
    if args.chunk_blocks < 1 or args.margin < 0:
        parser.error("--chunk-blocks must be positive and --margin not negative")

    rpc = Rpc(args.rpcurl or rpc_url_from_datadir(args.datadir, None, args.rpcport))
    info = rpc.call("getblockchaininfo")
    network = info["chain"]
    if network not in NETWORKS:
        raise SystemExit("unknown network %r" % network)
    if info.get("initialblockdownload"):
        raise SystemExit("the node is still synchronizing")
    magic = NETWORKS[network]["magic"]
    limit = info["blocks"] - args.margin

    os.makedirs(args.out, exist_ok=True)
    manifest_path = os.path.join(args.out, "manifest.json")
    chunks = []
    if os.path.exists(manifest_path):
        with open(manifest_path) as f:
            old = json.load(f)
        if old.get("format") != FORMAT_VERSION or old.get("network") != network:
            raise SystemExit("%s is for another format or network" % manifest_path)
        chunks = old["chunks"]
        # what is there must still be part of the chain of this node
        for chunk in chunks:
            if rpc.call("getblockhash", chunk["last_height"]) != chunk["last_hash"]:
                raise SystemExit("%s is not on the chain of this node (height %d). Start over in an empty directory."
                                 % (chunk["file"], chunk["last_height"]))
            if not os.path.exists(os.path.join(args.out, chunk["file"])):
                raise SystemExit("%s is listed in the manifest but missing" % chunk["file"])

    next_height = chunks[-1]["last_height"] + 1 if chunks else 1   # the genesis block is part of the node
    started = time.time()
    written = 0
    while next_height <= limit:
        # a chunk ends where the next multiple of the chunk size begins, or at the limit
        last = min((next_height // args.chunk_blocks + 1) * args.chunk_blocks - 1, limit)

        def progress(height, first=next_height, last=last):
            sys.stderr.write("\rblocks %d-%d: %d   " % (first, last, height))
            sys.stderr.flush()

        chunk = write_chunk(rpc, magic, args.out, next_height, last, not args.no_compress, args.level,
                            args.max_file_mb * 1000000, progress)
        last = chunk["last_height"]
        sys.stderr.write("\r%s: %d blocks, %.1f MB -> %.1f MB\n" % (
            chunk["file"], last - next_height + 1, chunk["size"] / 1e6, chunk["compressed_size"] / 1e6))
        chunks.append(chunk)
        written += 1
        next_height = last + 1
        # the manifest is complete after every chunk: an interrupted run loses nothing
        manifest = {
            "format": FORMAT_VERSION,
            "network": network,
            "generated": int(time.time()),
            "tip_height": chunks[-1]["last_height"],
            "tip_hash": chunks[-1]["last_hash"],
            "total_size": sum(c["size"] for c in chunks),
            "total_compressed_size": sum(c["compressed_size"] for c in chunks),
            "chunks": chunks,
        }
        with open(manifest_path + ".tmp", "w") as f:
            json.dump(manifest, f, indent=1)
            f.write("\n")
        os.replace(manifest_path + ".tmp", manifest_path)
        # for those who fetch the files by other means: sha256sum -c SHA256SUMS
        with open(os.path.join(args.out, "SHA256SUMS.tmp"), "w") as f:
            for c in chunks:
                f.write("%s  %s\n" % (c["sha256"], c["file"]))
            f.write("%s  manifest.json\n" % sha256_of(manifest_path))
        os.replace(os.path.join(args.out, "SHA256SUMS.tmp"), os.path.join(args.out, "SHA256SUMS"))

    if written == 0:
        print("nothing to add: the manifest reaches height %d, the limit is %d" % (next_height - 1, limit))
    else:
        total = sum(c["compressed_size"] for c in chunks)
        print("%d chunk(s) written in %.0f s; %d chunk(s) up to height %d, %.1f MB to download"
              % (written, time.time() - started, len(chunks), chunks[-1]["last_height"], total / 1e6))


if __name__ == "__main__":
    main()
