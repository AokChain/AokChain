# contrib/aokchain

## make_bootstrap.py — block archives for the fast first synchronization

Writes the blocks of a synced node as archives that other nodes fetch with
`-bootstrapurl=<address>` or import with `-bootstrapdir=<folder>`
(see `doc/aokchain/12-bootstrap.md`).

```
contrib/aokchain/make_bootstrap.py --datadir ~/.aokchain --out /srv/www/bootstrap
```

- Reads the active chain through RPC (`getblockhash`, `getblock <hash> 0`) of a running, synced node.
  `--datadir` takes the RPC cookie or `rpcuser`/`rpcpassword` from there; `--rpcurl http://user:pass@host:port` works too.
- Writes `blocks-<first>-<last>.dat.gz` (bootstrap.dat format, gzip), `manifest.json` and `SHA256SUMS`
  into one flat folder. A chunk has 250,000 heights at most and ends earlier when its file reaches
  190 MB (`--chunk-blocks`, `--max-file-mb`). It stops 1,000 blocks below the tip (`--margin`).
- Run it again later: chunks that are listed in the manifest are never rewritten, only new ones are
  added, and `manifest.json` and `SHA256SUMS` are replaced.
- The folder is ready to publish as it is: serve it over HTTP, copy it to mirrors, upload the files as
  release assets (`publish_bootstrap_github.md`), or make a torrent of the folder.

The host of the files does not have to be trusted: a node validates every block of an archive like a
block from a peer. The SHA-256 of the manifest only detects damaged downloads early.

## make_snapshot.py — chain state snapshot for the fast start

Packages the databases of a stopped node (chainstate, block index, tokens, governance) as
`snapshot-<height>.partNN` files of 190 MB at most plus `snapshot.json`, into the folder of the
block archives, and prints the values that `src/chainparams.cpp` must hold for a program to
accept this snapshot (see `doc/aokchain/15-snapshot.md`).

```
aokchain-cli getstatehashes > hashes.json && aokchain-cli stop
contrib/aokchain/make_snapshot.py --datadir ~/snapshot-node --hashes hashes.json --out /srv/www/bootstrap
```

- The node must have been synchronized from the block archives only (`connect=0`, `txindex=0`),
  to the height where they end. A snapshot of any other node is rejected by the check of the
  node that fetches it.
- The files are not trusted by anyone: a node computes the hashes of what it unpacked and compares
  them with the values of its program. Publishing a snapshot therefore always goes with a program
  release that has its values.

## publish_bootstrap_github.md

How to publish the folder as GitHub release assets (Korean).
