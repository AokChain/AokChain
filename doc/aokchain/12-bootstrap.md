# Fast first synchronization (block bootstrap)

When a freshly installed wallet fetches 3 million blocks one by one from peers, it takes hours. The slow part is not validation but the **download** (there are few peers and they are old). So two things were done.

1. **assumevalid update** — something that is done with every release. Below a block read from a node that has finished full validation, signature (script) checks are skipped.
2. **Block bootstrap** — files in which blocks have been bundled in advance are fetched over HTTP(S) or read from a folder and imported. The imported blocks are **validated in full, exactly like blocks received from peers**.

The consensus rules, the checkpoints and the wallet file format were not changed. Mainnet has a **built-in default address** (GitHub release assets, see "Options" below). With `-bootstrap=0` it is not used. Testnet and regtest have no default address.

## 1. assumevalid update

Only three mainnet values in `src/chainparams.cpp` were changed (commit `13222ef`).

| Value | Contents |
|---|---|
| `defaultAssumeValid` | Block **3,030,000** `27a0660bf43381e7d367472a74d696873ff69bb36582031f9c2ed2ae1c58bdf1` |
| `nMinimumChainWork` | The chainwork of that block, `…044f3a55ad8aa699012a` |
| `chainTxData` | Time 1794237264, 8,223,433 transactions, 0.0301 per second (`getchaintxstats 43200 <hash>`) |

- The values were read with `getblockhash 3030000`, `getblockheader` and `getchaintxstats` on the verification node (tip 3,032,417; a node that checked every script of the whole chain with `-reindex -par=8 -assumevalid=0`, see `01-status.md`).
- Effect: a node syncing for the first time skips the script checks of the blocks below that block **once it has received the headers up to that block**. The other checks (proof of work/stake, merkle, double spends, amounts, token rules) are done as before. With `-assumevalid=0` all scripts are checked too.
- The old value was around block 1.6 million, so the 1.4 million blocks above it were checked in full every time.

## 2. Block bootstrap

### Trust model

- The blocks inside an archive go through `ProcessNewBlock()` one at a time, in order (`CheckBlock` → `AcceptBlock` → `ActivateBestChain`/`ConnectBlock`). This is **exactly the call that a block received from a peer goes through**. The server that provides the files therefore does not have to be trusted.
- `LoadExternalBlockFile()`, which `-loadblock` uses, is not used. See "Note on the import order" below.
- A wrong or damaged archive **simply fails**. The node then stands at the height of the valid chain it had reached by then and continues from peers as usual. A block that validation rejected is never marked valid.
- The SHA-256 in `manifest.json` is not a security mechanism but a way to **notice a damaged download early** (the manifest itself is not signed).
- The only bad things a server can do are "not serving" and "serving something old", and both fall back to peer synchronization.

### How it works

1. At startup, if there is an address (`-bootstrapurl`, otherwise the mainnet default address) or a folder (`-bootstrapdir`), the bootstrap thread runs. **If the block at the chain tip is less than 3 days old, it ends without asking the server anything** (a wallet that is already synced does not contact the server on every start). Otherwise it reads `manifest.json`.
2. If the end height of the archives is at least `-bootstrapminblocks` (default 20,000) ahead of my height, it starts. Otherwise it ends with "not needed".
3. It fetches the archives in order, starting with the one that contains the block after my height. The size and SHA-256 of each fetched file are compared with the manifest. The fetching thread runs at most **2 archives** ahead of the import (to save disk space).
4. A fetched archive is decompressed and its blocks are handed over **one at a time, each after the previous block has been connected to the chain**. For each block it checks that it entered the chain, and at the end of the archive it checks that **the last block hash from the manifest is in my chain**. A finished archive file is deleted immediately.
5. Meanwhile **only headers** are received from peers (block requests are held back). The headers are needed for assumevalid to take effect.
6. When it finishes, fails or is skipped by the user, block download from peers resumes immediately and `<datadir>/bootstrap/` is deleted. The archives end about 1,000 blocks below the tip, so the rest comes from peers.
7. If the node is shut down midway the files stay, and on the next start the download is resumed (Range) and the import continues. Blocks that are already present are skipped.

Cases treated as failure: the manifest cannot be fetched / has a format error / is for another network, a file cannot be fetched from any mirror, SHA-256 mismatch, decompression error (CRC, length), a block is rejected (the chain does not keep up), the number of blocks or the last hash of an archive differs from the manifest.

### Options

| Option | Meaning |
|---|---|
| `-bootstrapurl=<address>` | Base address where the archives are. If it is not given, mainnet uses the built-in address `https://github.com/AokChain/Mainnet-blocks/releases/download/mainnet/` (it is HTTPS, so **only the GUI wallet** can use it; `aokchaind` logs that fact and continues without the bootstrap). Given several times, they are mirrors: the manifest of the first one that answers is used, and for each file they are tried in order, moving on to the next mirror on an error, timeout or hash mismatch |
| `-bootstrapdir=<folder>` | Import from a folder on this computer (USB, file sharing, fetched by torrent). The same manifest and SHA-256 checks and the same full validation. **The folder is only read and never deleted.** No network is needed |
| `-bootstrap=0` | Do not use it. The built-in address is not used either (default 1) |
| `-bootstrapminblocks=<n>` | Use it only when at least this far behind (default 20000) |

RPC: `getbootstrapinfo` (state, number of archives, bytes fetched, height, time taken, message), `skipbootstrap` (stop and fetch from peers).

### Note on the import order (a property of the existing code)

At first the blocks were handed over 2,000 at a time with `LoadExternalBlockFile()`, like `-loadblock`, but near mainnet block 902,350 a **valid block was rejected** (`bad-txns-issue-unique-token-burn-outpoints-not-found`). The cause is in the existing validation code. The token issuance fee check inside `AcceptBlock()` (`CheckIssueBurnTx`) reads the **governance state at the current chain tip** (issuance costs, fee address), not the state at the time of that block. So when a block far ahead of the chain tip goes through `AcceptBlock` first, in a range where governance changed the costs in between it is judged with the old costs and marked invalid.

- The bootstrap avoids this by connecting each block before handing over the next one. The whole of mainnet passed this way (measurements below).
- **The validation code was not changed.** Because of the same property, the same rejection can happen in peer synchronization too, if a governance change falls inside the range in which blocks arrive ahead of the chain tip (at most 1,024 blocks). It has not shown up in peer synchronization so far (the download is slow, so blocks were processed almost in order), but it is an item that has to be dealt with separately.

### HTTP and HTTPS

| Program | Addresses it can fetch from |
|---|---|
| **aokchain-qt (GUI)** | `http://`, **`https://`**, redirects (https→http is refused). Uses the Qt network stack (`QNetworkAccessManager`) and the system certificate store. If the certificate cannot be verified, that mirror is treated as failed. Places that are HTTPS + 302, such as GitHub release assets, can be used as they are |
| **aokchaind** | Only `http://` and http redirects (libevent). The libevent in depends is built without OpenSSL, so HTTPS was not added. Fetch files on an HTTPS host with `curl`/`wget`/`gh` and import them with `-bootstrapdir` |

Both use the same import and validation code. Only the fetching part is swapped (`SetBootstrapFetcher`).

### Compression

The archives are **gzip**. Instead of attaching a new compression library to the node, a small gzip decompressor was added (`src/gunzip.cpp`, about 330 lines). zlib is present only inside Qt and is not in depends for the mingw daemon, so this was simpler for both builds than adding a link dependency. The input is not trusted (all length and distance checks, an upper bound on the output length, CRC-32 and length verification). zstd and xz were not included.

### File format

The files sit side by side under one folder (one base address). There are no subfolders.

- `blocks-<first height, 8 digits>-<last height, 8 digits>.dat.gz` — bootstrap.dat format: for each block 4 bytes of network magic, 4 bytes of length (little endian), the raw block. In height order. One gzip stream.
- `manifest.json`:

```
{"format":1, "network":"main", "generated":<time>, "tip_height":…, "tip_hash":"…",
 "total_size":…, "total_compressed_size":…,
 "chunks":[{"file":"blocks-00000001-00230000.dat.gz", "first_height":1, "last_height":230000,
            "first_hash":"…", "last_hash":"…", "size":<uncompressed>, "compressed_size":<file size>,
            "compression":"gzip", "sha256":"<SHA-256 of the file>"}, …]}
```

- `SHA256SUMS` — for `sha256sum -c`. Covers the archives and manifest.json.

Manifests the node accepts: `format` 1, `network` is its own network, file names consist only of alphanumerics and `.-_` (no paths), the heights are contiguous, size 8 GiB or less.

### Creating and publishing

```
contrib/aokchain/make_bootstrap.py --datadir <datadir of a synced node> --out <folder>
```

- Reads the active chain in height order through the RPC of a running node (`getblockhash`, `getblock <hash> 0`). It does not read the blk files directly, so no orphan blocks get mixed in and the node does not have to be stopped.
- One archive has at most 250,000 blocks, and it is cut where the file reaches 190 MB (in consideration of the file size limits of free hosts and of resuming downloads). It writes only up to 1,000 blocks below the tip.
- When run again it **only adds new archives** and does not touch the existing files. Only `manifest.json` and `SHA256SUMS` change.

Ways of distribution (the same folder is used as it is):

| Method | On the wallet side |
|---|---|
| Static host, CDN, own web server | `bootstrapurl=<address>` |
| GitHub release assets | GUI: `bootstrapurl=https://github.com/<owner>/<repository>/releases/download/<tag>/`. Daemon: download and use `-bootstrapdir`. The procedure is in `contrib/aokchain/publish_bootstrap_github.md` |
| Several mirrors | Several `bootstrapurl=` lines |
| Torrent, USB, file sharing | The wallet has no torrent support. The user points `-bootstrapdir` or "Choose a folder with block archives..." at the folder they downloaded |

### GUI

- Sync line of the sidebar: "Fast sync: archive 3/15 · 412 MB / 1.4 GB", and once everything has been fetched "Validating blocks · 1,250,000 / 3,031,432".
- Sync information window: an explanation (that the block archives are fetched from a server and every block is validated, so the server does not have to be trusted), the progress, **"Skip and get the blocks from peers"**, **"Choose a folder with block archives..."**.
- Settings → Network: on/off, several addresses (one per line; when left empty, the built-in mainnet address), a folder selection button. Changing the addresses requires a restart.

## Measurements (2026-11-11)

### Archives

Mainnet blocks 1 ~ 3,031,432 (about 1,000 below the tip) were written on the verification node. Creating them took 15 minutes 41 seconds (about 3,200 blocks per second through RPC).

| Item | Value |
|---|---|
| Number of files | 15 archives + `manifest.json` + `SHA256SUMS` |
| Uncompressed | 2,127 MB |
| Compressed (amount to download) | **1,409 MB** (66%) |
| Largest file | 190.3 MB (followed by 190.1, 181.7, 114.1 MB … the smallest is 8.5 MB) |
| Decompression (built-in decompressor) | 6.7 seconds for one 287 MB archive (about 43 MB/s; `gzip -d` takes 2.7 seconds). About 50 seconds in total, included in the import time |

The early block ranges have many transactions, so 250,000 blocks exceed 190 MB and were split in two (1~230,000 / 230,001~249,999, 250,000~425,249 / 425,250~499,999). The rest are 250,000 blocks each.

### From scratch to the tip

Empty data directory, default settings (dbcache 450 MB, the new assumevalid), mainnet.

| | Download | Import (decompression + validation) | Total to the tip | Notes |
|---|---|---|---|---|
| **Build server, `aokchaind`**, from a web server on the LAN (`http://`) | 4 s | 2,499 s | **41 min 57 s** (2,517 s) | The remaining 1,074 blocks came from peers within 15 seconds afterwards |
| **Second server, `aokchaind`**, downloaded from GitHub with `curl`, then `-bootstrapdir` | 151 s (9.3 MB/s) | 2,302 s + 5 s for the SHA-256 check | **41 min 22 s** (2,482 s) | The operator's way. Disk at most 4.7 GB + 1.4 GB for the downloaded folder |
| **Second server, `aokchain-qt`** (headless, `QT_QPA_PLATFORM=offscreen`), **built-in address** (GitHub, HTTPS + 302) | 110 s (overlapping with the import) | 2,755 s | **46 min 37 s** (2,797 s) | The Qt downloader fetched all 15 files from the real GitHub address. Disk at most 4.7 GB |
| Build server, `aokchaind`, LAN, **`-assumevalid=0`** | 4 s | (not measured to the end) | 2,269,037 blocks at 43 minutes | At the same point the default configuration had already finished. Because of a mistake in the measurement script it stopped at block 2,477,424. Run to the end it looks like 55~60 minutes, but that is not a measured value |
| (for comparison) peers only, earlier measurement | — | — | about 4 hours | `01-status.md`, `-par=1 -assumevalid=0` |
| (for comparison) reindex of local files, earlier measurement | — | — | about 57 minutes | `-reindex -par=8 -assumevalid=0`, no download |

- Both servers are virtual machines on an AMD EPYC 7401P with 8 cores, 15 GB of memory and an SSD. During validation **only one core was at 100%** (when assumevalid skips the script checks, the remaining work is connecting blocks and writing the databases, which is single-threaded). Memory peaked at 2.4 GB.
- The tip hash and `gettxoutsetinfo` of the synced nodes were **the same** as on the verification node: on the build server at height 3,032,506 `hash_serialized_2` `66f88037…00774` (7,151,800 transactions, 7,277,177 outputs, total 2,104,125,927.67543002 AOK), on the second server (the `-bootstrapdir` node) at height 3,032,548 `8d1f3b15…ed3d3`. For the node synced with the GUI, the hashes of block 3,031,000 and of the tip 3,032,548 were the same as on the verification node (`gettxoutsetinfo` was not compared).
- The Windows build was checked under wine up to the point where it fetched the manifest and the first 2 archives from the built-in address (GitHub) and started importing (it was not run to the end).
- The headers were fully received from peers in about two and a half minutes (3,032,450 headers). assumevalid applies to the blocks imported after that.

### On a slow PC

The download is determined by the line speed (1.4 GB: 2 minutes at 100 Mbps, 20 minutes at 10 Mbps). The rest is **validation time and depends on the speed of one core and on the disk**. With the cores of the build server (a 2017 EPYC, 2.0 GHz) it takes 40 minutes, so 20~30 minutes are expected on a current desktop and 1~3 hours on an old laptop or an HDD. This estimate has not been measured. Unlike the 4 hours from peers (where the download is the bottleneck), it gets faster in proportion to the performance of the PC.

## Limitations

- **The manifest is not signed.** It was designed so that this is not needed (every block is validated), but a server that serves an old manifest cannot be told apart. In that case less is fetched and the rest comes from peers.
- **aokchaind cannot do HTTPS** (table above).
- If the archives are far behind the tip, that much is fetched from peers. Archives have to be added periodically.
- While fetching, 2~3 extra archives (compressed files + one decompressed, about 0.9 GB at most) are on disk for a short time.
- Only fetching the blocks gets faster. **The validation time is unchanged**, so on a slow PC validation takes up most of the time.
- If it is skipped midway, the blocks imported up to then stay and the rest is fetched from peers.
- Not tested on pruning nodes.

## What was not done: state snapshots (the assumeUTXO approach)

To remove the validation time as well, there is the approach of fetching a **snapshot of the UTXO set** instead of blocks, using it right away and validating the historical blocks in the background (Bitcoin Core's assumeutxo). Reasons for not doing it now:

- The hash of the snapshot has to be **hard-coded in the source**, and if that value is wrong, fake balances are believed. Unlike block archives, it is not a case of "if it is wrong, it simply fails".
- Besides the UTXO set, this chain has a separate **token database, governance database and stake-related state**, all of which would have to go into the snapshot and be bound by the hash. The validation code (maintaining two chain states) would also be changed heavily.
- With block archives alone the first synchronization drops from 4 hours to under 1 hour, which resolves the most urgent bottleneck right now (the download).

It can be done as a next step if it becomes necessary.
