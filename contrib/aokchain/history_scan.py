#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Scan the whole chain of a synced node for blocks/transactions that would
violate candidate soft-fork rules (doc/aokchain/07-upstream-backports.md, B).

Run against a fully validating node (-txindex). Example:

  contrib/aokchain/history_scan.py --datadir=/path/to/datadir --out=scan.json

Checks
  S-05  proof-of-stake block time not aligned to STAKE_TIMESTAMP_MASK (15)
  S-01  token output amount <= 0 or > MAX_MONEY_TOKENS, or per-tx per-token
        output sum > MAX_MONEY_TOKENS
  S-02  issued + reissued supply of a token > MAX_MONEY_TOKENS
  S-03  token output carrying a non-zero AOK value
  S-04  token amount not a multiple of 10^(8 - units) (final units)
  S-10  coinbase / coinstake transaction carrying token outputs

The script only reads via RPC. It is read-only with respect to the node.
"""

import argparse
import base64
import collections
import concurrent.futures
import http.client
import json
import os
import sys
import threading
import time

COIN = 100000000
MAX_MONEY_TOKENS = 25000000000 * COIN
STAKE_TIMESTAMP_MASK = 15
LAST_POW_BLOCK = 1440          # mainnet consensus.nLastPOWBlock
TOKENS_HEIGHT = 159500         # mainnet token activation


class RPC:
    def __init__(self, host, port, cookie_path):
        self.host, self.port, self.cookie_path = host, port, cookie_path
        self.local = threading.local()

    def _auth(self):
        # Re-read every time: the cookie changes when the node restarts.
        with open(self.cookie_path) as f:
            return base64.b64encode(f.read().strip().encode()).decode()

    def _conn(self):
        if getattr(self.local, "conn", None) is None:
            self.local.conn = http.client.HTTPConnection(self.host, self.port, timeout=600)
        return self.local.conn

    def batch(self, calls):
        body = json.dumps([{"jsonrpc": "1.0", "id": i, "method": m, "params": p}
                           for i, (m, p) in enumerate(calls)])
        for attempt in range(30):
            try:
                headers = {"Authorization": "Basic " + self._auth(), "Content-Type": "application/json"}
                conn = self._conn()
                conn.request("POST", "/", body, headers)
                resp = json.loads(conn.getresponse().read())
                if isinstance(resp, list):
                    break
            except (http.client.HTTPException, OSError, ValueError):
                # node restarting, warming up (-28), or auth cookie rotated
                pass
            self.local.conn = None
            time.sleep(min(2 + attempt, 30))
        else:
            raise RuntimeError("RPC batch failed")
        resp.sort(key=lambda r: r["id"])
        for r in resp:
            if r.get("error"):
                raise RuntimeError(r["error"])
        return [r["result"] for r in resp]

    def call(self, method, *params):
        return self.batch([(method, list(params))])[0]


def scan_range(rpc, start, end, batch_size):
    """Return (violations, token_events, stats) for heights [start, end)."""
    violations = []
    events = []  # (height, txid, kind, name, satoshis)
    stats = collections.Counter()
    for h0 in range(start, end, batch_size):
        heights = list(range(h0, min(h0 + batch_size, end)))
        hashes = rpc.batch([("getblockhash", [h]) for h in heights])
        verbosity = 2 if heights[-1] >= TOKENS_HEIGHT else 1
        blocks = rpc.batch([("getblock", [bh, verbosity]) for bh in hashes])
        for h, blk in zip(heights, blocks):
            stats["blocks"] += 1
            if h > LAST_POW_BLOCK:
                stats["pos_blocks"] += 1
                if blk["time"] & STAKE_TIMESTAMP_MASK:
                    violations.append({"rule": "S-05", "height": h, "hash": blk["hash"], "time": blk["time"]})
            if verbosity < 2:
                continue
            for idx, tx in enumerate(blk["tx"]):
                is_gen = idx == 0 or (idx == 1 and h > LAST_POW_BLOCK)
                per_token = collections.Counter()
                for o in tx["vout"]:
                    spk = o["scriptPubKey"]
                    tok = spk.get("token")
                    if tok is None:
                        continue
                    stats["token_outputs"] += 1
                    name, sat = tok["name"], int(tok["satoshis"])
                    ref = {"height": h, "txid": tx["txid"], "n": o["n"], "name": name, "satoshis": sat, "type": spk.get("type")}
                    if is_gen:
                        violations.append(dict(ref, rule="S-10"))
                    if o["value"] != 0:
                        violations.append(dict(ref, rule="S-03", value=o["value"]))
                    # A reissue may carry amount 0 (only units/IPFS change).
                    min_amount = 0 if spk.get("type") == "reissue_token" else 1
                    if sat < min_amount or sat > MAX_MONEY_TOKENS:
                        violations.append(dict(ref, rule="S-01"))
                    if spk.get("type") == "transfer_token":
                        per_token[name] += sat
                    elif spk.get("type") in ("new_token", "reissue_token"):
                        events.append((h, tx["txid"], spk["type"], name, sat))
                for name, total in per_token.items():
                    if total > MAX_MONEY_TOKENS:
                        violations.append({"rule": "S-01", "height": h, "txid": tx["txid"], "name": name, "sum": total})
                    stats["token_transfers"] += 1
                # remember amounts for the units check
                for o in tx["vout"]:
                    tok = o["scriptPubKey"].get("token")
                    if tok is not None:
                        events.append((h, tx["txid"], "amount", tok["name"], int(tok["satoshis"])))
    return violations, events, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--datadir", required=True)
    ap.add_argument("--rpcport", type=int, default=33440)
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--end", type=int, default=None, help="exclusive; default: tip+1")
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--batch", type=int, default=100)
    ap.add_argument("--chunk", type=int, default=20000)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rpc = RPC("127.0.0.1", args.rpcport, os.path.join(args.datadir, ".cookie"))
    tip = rpc.call("getblockcount")
    end = args.end if args.end is not None else tip + 1
    units = {name: info["units"] for name, info in rpc.call("listtokens", "*", True).items()}

    chunks = [(s, min(s + args.chunk, end)) for s in range(args.start, end, args.chunk)]
    violations, events, stats = [], [], collections.Counter()
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(args.threads) as ex:
        futs = {ex.submit(scan_range, rpc, s, e, args.batch): (s, e) for s, e in chunks}
        for i, fut in enumerate(concurrent.futures.as_completed(futs), 1):
            v, ev, st = fut.result()
            violations += v
            events += ev
            stats.update(st)
            s, e = futs[fut]
            print("[%d/%d] %d-%d done, %d violations so far, %.0fs" % (i, len(chunks), s, e, len(violations), time.time() - t0), flush=True)

    # S-02 supply and S-04 units, in chain order
    events.sort()
    supply = collections.Counter()
    for h, txid, kind, name, sat in events:
        if kind in ("new_token", "reissue_token"):
            supply[name] += sat
            if supply[name] > MAX_MONEY_TOKENS:
                violations.append({"rule": "S-02", "height": h, "txid": txid, "name": name, "supply": supply[name]})
        elif kind == "amount":
            base = name.rstrip("!")
            u = units.get(name, units.get(base))
            if name.endswith("!") or "#" in name:
                if sat != COIN:
                    violations.append({"rule": "S-04", "height": h, "txid": txid, "name": name, "satoshis": sat, "note": "owner/unique must be 1"})
            elif u is not None and sat % (10 ** (8 - u)) != 0:
                violations.append({"rule": "S-04", "height": h, "txid": txid, "name": name, "satoshis": sat, "units": u})

    by_rule = collections.Counter(v["rule"] for v in violations)
    result = {"tip": tip, "range": [args.start, end], "stats": dict(stats),
              "violations_by_rule": dict(by_rule), "violations": violations,
              "seconds": round(time.time() - t0)}
    with open(args.out, "w") as f:
        json.dump(result, f, indent=1)
    print(json.dumps({k: result[k] for k in ("tip", "range", "stats", "violations_by_rule", "seconds")}, indent=1))


if __name__ == "__main__":
    sys.exit(main())
