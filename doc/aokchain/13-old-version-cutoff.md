# Old version cutoff

AokChain Core 4.0 can stop peering with nodes of the 3.0.x series from a chosen block height on.
It is switched off in the source until a height is set.

## What it does

- 4.0 announces protocol version **70021**. 3.0.x announces 70020.
- Each network has an **old peer cutoff height** (`CChainParams::OldPeerCutoffHeight()`).
  On mainnet and testnet it is disabled (`INT_MAX`).
- While the active chain of a 4.0 node is **below** that height, nothing changes: peers from
  protocol version 60018 on are accepted, as before.
- Once the active chain has **reached** that height:
  - a peer that announces a version below 70021 gets the usual `obsolete` reject
    ("Version must be 70021 or greater") and is disconnected during the handshake;
  - a peer below 70021 that is already connected is disconnected the next time the node
    handles it (within about a second);
  - nobody is banned or discouraged for it. An old node that upgrades can connect at once.
- The cutoff is **not a consensus rule**. It does not change which blocks are valid. It only
  decides whom a 4.0 node talks to. Its purpose is to keep nodes that have not upgraded from
  feeding off, and relaying to, the upgraded network once the old series is retired.

A 4.0 node that is itself still synchronizing below the cutoff height keeps accepting old
peers until its own chain reaches the height. That is intended: it can take old blocks from
anyone, and block validation does not depend on who delivers a block.

3.0.x nodes accept a peer that announces 70021: their version handling only compares with
`>=` against feature versions and uses `min(peer version, own version)` for the wire format
(checked in `src/net_processing.cpp` of 3.0.5).

## Setting the mainnet height at release time

In `src/chainparams.cpp`, in the constructor of the main network parameters, add:

```
nOldPeerCutoffHeight = <height>;
```

Choose a height far enough ahead that operators have time to upgrade (blocks come about every
50 seconds: 1,700 blocks a day, 12,000 a week). Leave testnet disabled or set it earlier for
a rehearsal. On regtest the height is set with `-oldpeercutoffheight=<height>` (used by
`test/functional/p2p_old_peer_cutoff.py`).

## Rollout order

1. Give the 4.0 binaries, with the height set, to the exchanges and the large stakers first.
2. Confirm that the majority of the stake runs 4.0 (see below) before the height is announced
   as final.
3. Publish the binaries and the deadline (the height and its approximate date) to everyone.
4. Publish the source last.

If the upgrade goes slower than planned, release a new build with a later height before the
old height is reached. After the height, nodes with different heights still follow the same
chain; only their choice of peers differs.

## Counting upgraded peers

`getpeerinfo` lists for every peer its `version` (70021 for 4.0) and `subver`
(`/AokChain:4.0.0/`):

```
aokchain-cli getpeerinfo | grep -c '"version": 70021'
aokchain-cli getpeerinfo | grep '"subver"' | sort | uniq -c
```

This counts the peers of one node, not stake. For stake, look at who produces blocks: ask the
known large stakers directly, and watch whether blocks keep arriving at the usual rate on a
test node whose cutoff height is already reached.
