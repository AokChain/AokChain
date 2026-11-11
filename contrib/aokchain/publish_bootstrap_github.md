# Publishing block archives as GitHub release assets

This is the procedure for uploading the folder created by `make_bootstrap.py` (the block archives `blocks-*.dat.gz`, `manifest.json`, `SHA256SUMS`) as assets of a GitHub release. After the upload, a wallet that is given the release download address in `-bootstrapurl` fetches and uses those files.

- The address of a release asset has the form `https://github.com/<owner>/<repository>/releases/download/<tag>/<file>`, without subfolders. That is why the archive folder also has all its files side by side on one level.
- A single asset can be up to 2 GiB, but each file is kept at 190 MB or less so that resuming downloads works well (the default of the tool).
- GitHub serves HTTPS only and redirects to another server (302). **The GUI wallet (aokchain-qt) can use this address as it is.** `aokchaind` accepts only `http://` addresses, so server operators download the files with `curl`/`wget`/`gh` and import them with `-bootstrapdir=<folder>` (step 5 below).

## 1. Create the archives

On a computer where a fully synced node is running:

```
contrib/aokchain/make_bootstrap.py --datadir ~/.aokchain --out aokchain-bootstrap-main
cd aokchain-bootstrap-main && sha256sum -c SHA256SUMS
```

## 2. Create the release (once only)

Keep the tag name **fixed**. Here it is `bootstrap-mainnet`. The address then does not change when files are added later.

Web interface: repository → Releases → "Draft a new release" → enter `bootstrap-mainnet` in Tag (create a new tag) → title, for example "Mainnet block archives" → in the description "Block archives for the fast first synchronization of the wallet. The wallet validates every block." → **check "Set as a pre-release"** (so that it does not take the place of the latest release) → Publish.

Command line (`gh`):

```
gh release create bootstrap-mainnet --repo <owner>/<repository> --prerelease \
  --title "Mainnet block archives" \
  --notes "Block archives for the fast first synchronization of the wallet. The wallet validates every block."
```

## 3. Upload the files

Web interface: Edit on the release → drag **all files** in the folder onto "Attach binaries" (every `blocks-*.dat.gz`, `manifest.json`, `SHA256SUMS`) → Update release.

Command line:

```
cd aokchain-bootstrap-main
gh release upload bootstrap-mainnet --repo <owner>/<repository> blocks-*.dat.gz manifest.json SHA256SUMS
```

Check after the upload (on any computer):

```
curl -sL https://github.com/<owner>/<repository>/releases/download/bootstrap-mainnet/manifest.json | head
```

## 4. Use it in the wallet

In `aokchain.conf`, or in the block archive addresses field under Settings → Network:

```
bootstrapurl=https://github.com/<owner>/<repository>/releases/download/bootstrap-mainnet/
```

- The base address includes the trailing `/`. The wallet appends `manifest.json` and each file name to it when fetching.
- To have more mirrors, write several `bootstrapurl=` lines. They are tried in order from the top.
- An address that is put into the source as the default must be the **same string**. The mainnet default built into the source is `https://github.com/AokChain/Mainnet-blocks/releases/download/mainnet/` (see `doc/aokchain/12-bootstrap.md`).

## 5. Use it with aokchaind (server)

`aokchaind` does not do HTTPS. Download the files and hand over the whole folder:

```
mkdir bootstrap && cd bootstrap
gh release download bootstrap-mainnet --repo <owner>/<repository>      # or curl -LO for each file
sha256sum -c SHA256SUMS
aokchaind -bootstrapdir=$PWD
```

The folder is only read and never deleted. Delete it yourself when the synchronization has finished.

## 6. Add blocks later

Running the tool again in the same folder creates only new archives and changes `manifest.json` and `SHA256SUMS`. The existing archive files stay as they are.

```
contrib/aokchain/make_bootstrap.py --datadir ~/.aokchain --out aokchain-bootstrap-main
cd aokchain-bootstrap-main
gh release upload bootstrap-mainnet --repo <owner>/<repository> --clobber manifest.json SHA256SUMS <the new blocks-*.dat.gz files>
```

- **Upload the new archive files first and replace `manifest.json` last.** In the opposite order, a wallet that fetches in between looks for files that are not there yet (even then the wallet merely fails and fetches from peers).
- In the web interface: Edit → delete the existing `manifest.json` and `SHA256SUMS` and upload the new files.
- The last archive is uploaded without being full. The next run creates a new file starting at the height after it (existing files are not rewritten). If the number of files grows too large, everything can be recreated from scratch in a new folder and replaced completely.

## Other ways of distribution

The same folder is used as it is.

- **Static host, CDN, own web server**: upload the folder and put its address into `bootstrapurl`. Support for resuming (Range) is desirable.
- **Mirrors**: the same files in several places. Several `bootstrapurl` lines.
- **Torrent**: make a torrent of the folder (`aokchain-bootstrap-main`) as it is. The wallet has no torrent support, so users download it with their own torrent program and then import it with `-bootstrapdir=<folder>` or with "Choose a folder with block archives..." in the sync window.
- **USB, file sharing**: same as above.
