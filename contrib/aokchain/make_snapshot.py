#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Package the chain state of a stopped node as a snapshot.

A new node that finds the snapshot at its -bootstrapurl or -bootstrapdir (next
to the block archives of make_bootstrap.py) unpacks it and starts at the height
of the snapshot instead of validating every block from the first one. It does
so only when the content has the hashes that are compiled into the program
(CChainParams::Snapshot() in src/chainparams.cpp): the files written here are
not trusted by anyone, and a snapshot that differs in one unspent output is
removed again by the node that fetched it.

How to make one:

  1. Synchronize a node from the block archives only, to the height at which
     they end (aokchain.conf: connect=0, bootstrapdir=<the archives>). No peer
     must have sent it blocks: the block index may hold the active chain only,
     and the blocks must lie in the block files in the order of their heights.
     Keep the default index options (txindex=1, tokenindex=1).
  2. aokchain-cli getstatehashes > hashes.json
  3. aokchain-cli stop, and wait until the node is gone.
  4. make_snapshot.py --datadir <its data directory> --hashes hashes.json --out <directory of the archives>
  5. Put the printed values into src/chainparams.cpp and build. A node of that
     build accepts this snapshot and no other.

The snapshot holds the directories chainstate, blocks/index, tokens and
governance, not the block files. It is one gzip stream that is cut into parts
of 190 MB at most (the size limit of some hosts), listed in snapshot.json with
their SHA-256. The lines of the parts are added to SHA256SUMS.

Example:
  make_snapshot.py --datadir ~/snapshot-node --hashes hashes.json --out /srv/www/bootstrap
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import time
import zlib

FORMAT_VERSION = 1
MANIFEST = "snapshot.json"
DIRECTORIES = ["chainstate", "blocks/index", "tokens", "governance"]
# files of LevelDB that a copy does not need
SKIP = {"LOCK", "LOG", "LOG.old"}
SUBDIRS = {"main": "", "test": "testnet3", "regtest": "regtest"}


class PartWriter:
    """Writes a byte stream as files of a maximum size."""

    def __init__(self, out_dir, prefix, max_bytes):
        self.out_dir, self.prefix, self.max_bytes = out_dir, prefix, max_bytes
        self.parts = []
        self.file = None
        self.hash = None
        self.size = 0

    def _open(self):
        name = "%s.part%02d" % (self.prefix, len(self.parts) + 1)
        self.file = open(os.path.join(self.out_dir, name + ".tmp"), "wb")
        self.hash = hashlib.sha256()
        self.size = 0
        self.name = name

    def _close(self):
        self.file.close()
        os.replace(os.path.join(self.out_dir, self.name + ".tmp"), os.path.join(self.out_dir, self.name))
        self.parts.append({"file": self.name, "size": self.size, "sha256": self.hash.hexdigest()})
        self.file = None

    def write(self, data):
        while data:
            if self.file is None:
                self._open()
            piece = data[: self.max_bytes - self.size]
            self.file.write(piece)
            self.hash.update(piece)
            self.size += len(piece)
            data = data[len(piece):]
            if self.size == self.max_bytes:
                self._close()

    def finish(self):
        if self.file is not None:
            self._close()
        return self.parts


def snapshot_files(datadir):
    """The files of the snapshot as (name in the snapshot, path), in a fixed order."""
    result = []
    for directory in DIRECTORIES:
        path = os.path.join(datadir, *directory.split("/"))
        if not os.path.isdir(path):
            sys.exit("%s is missing: is this the data directory of a node with the default indexes?" % path)
        for name in sorted(os.listdir(path)):
            full = os.path.join(path, name)
            if name in SKIP or not os.path.isfile(full):
                continue
            result.append((directory + "/" + name, full))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datadir", required=True, help="data directory of the stopped node (the one given to -datadir)")
    parser.add_argument("--network", choices=sorted(SUBDIRS), default="main")
    parser.add_argument("--hashes", required=True, help="file with the output of getstatehashes, taken before the node was stopped")
    parser.add_argument("--out", required=True, help="directory for the parts and snapshot.json (the directory of the block archives)")
    parser.add_argument("--max-file-mb", type=int, default=190, help="size of a part at most, in MB of 1,000,000 bytes (default 190)")
    parser.add_argument("--level", type=int, default=6, help="gzip level 1-9 (default 6)")
    args = parser.parse_args()

    datadir = os.path.join(os.path.expanduser(args.datadir), SUBDIRS[args.network])
    for running in ("aokchaind.pid", ):
        if os.path.exists(os.path.join(datadir, running)):
            sys.exit("%s exists: stop the node first" % os.path.join(datadir, running))
    with open(args.hashes) as f:
        hashes = json.load(f)
    for key in ("height", "block", "utxo", "tokens", "governance", "index"):
        if key not in hashes:
            sys.exit("%s is not the output of getstatehashes (no \"%s\")" % (args.hashes, key))

    os.makedirs(args.out, exist_ok=True)
    files = snapshot_files(datadir)
    total = sum(os.path.getsize(path) for _, path in files) or 1
    prefix = "snapshot-%08d" % hashes["height"]
    # parts of an earlier snapshot of this height
    for name in os.listdir(args.out):
        if name.startswith(prefix + ".part"):
            os.remove(os.path.join(args.out, name))

    writer = PartWriter(args.out, prefix, args.max_file_mb * 1000000)
    packer = zlib.compressobj(args.level, zlib.DEFLATED, 31)  # 31: gzip container
    size = 0
    done = 0
    start = time.time()

    def put(data):
        nonlocal size
        size += len(data)
        writer.write(packer.compress(data))

    for name, path in files:
        encoded = name.encode("ascii")
        length = os.path.getsize(path)
        put(struct.pack("<H", len(encoded)) + encoded + struct.pack("<Q", length))
        copied = 0
        with open(path, "rb") as f:
            while True:
                block = f.read(1 << 20)
                if not block:
                    break
                put(block)
                copied += len(block)
        if copied != length:
            sys.exit("%s changed while it was read: is the node stopped?" % path)
        done += length
        sys.stderr.write("\r%d of %d files, %.0f%%" % (files.index((name, path)) + 1, len(files), 100.0 * done / total))
    put(struct.pack("<H", 0))
    writer.write(packer.flush())
    parts = writer.finish()
    sys.stderr.write("\n")

    manifest = {
        "format": FORMAT_VERSION,
        "network": args.network,
        "generated": int(time.time()),
        "height": hashes["height"],
        "block_hash": hashes["block"],
        "size": size,
        "compressed_size": sum(part["size"] for part in parts),
        "files": len(files),
        # for the reader; a node compares what it computes with the values of its program, not with these
        "hashes": {key: hashes[key] for key in ("utxo", "utxo_legacy", "tokens", "governance", "index", "coins") if key in hashes},
        "parts": parts,
    }
    with open(os.path.join(args.out, MANIFEST + ".tmp"), "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    os.replace(os.path.join(args.out, MANIFEST + ".tmp"), os.path.join(args.out, MANIFEST))

    # SHA256SUMS: the lines of the other files stay
    sums_path = os.path.join(args.out, "SHA256SUMS")
    lines = []
    if os.path.exists(sums_path):
        with open(sums_path) as f:
            lines = [line.rstrip("\n") for line in f if line.strip() and not line.split()[-1].lstrip("*").startswith("snapshot")]
    for part in parts:
        lines.append("%s  %s" % (part["sha256"], part["file"]))
    with open(os.path.join(args.out, MANIFEST), "rb") as f:
        lines.append("%s  %s" % (hashlib.sha256(f.read()).hexdigest(), MANIFEST))
    with open(sums_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    print("snapshot of height %d: %d files, %.1f MB, packed %.1f MB in %d part(s), %.0f s" % (
        hashes["height"], len(files), size / 1e6, manifest["compressed_size"] / 1e6, len(parts), time.time() - start))
    print()
    print("Values for src/chainparams.cpp (the network \"%s\"):" % args.network)
    print()
    print("        snapshot.height = %d;" % hashes["height"])
    for key in ("block", "utxo", "tokens", "governance", "index"):
        print("        snapshot.%s = uint256S(\"%s\");" % (key, hashes[key]))
    print()
    print("For a test on regtest: -snapshotparams=%s" % ":".join(
        [str(hashes["height"])] + [hashes[key] for key in ("block", "utxo", "tokens", "governance", "index")]))


if __name__ == "__main__":
    main()
