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

## publish_bootstrap_github.md

How to publish the folder as GitHub release assets.
