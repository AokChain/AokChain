# 02. Build and test guide (for the build server)

## Environment

| Item | Value |
|---|---|
| OS | Ubuntu 24.04.5 LTS, x86_64, 8 cores, 15GB RAM, 48GB disk (30GB free as of 2026-11) |
| Compiler | gcc/g++ 13.3, **C++17** |
| Boost | 1.83 (system) |
| OpenSSL | 3.0.13 (system) |
| libevent | 2.1.12 |
| BerkeleyDB | **5.3.28** (system) — requires `--with-incompatible-bdb` |
| ZeroMQ | 4.3.5 |
| miniupnpc | 2.2.6 |
| autoconf / automake / libtool | 2.71 / 1.16.5 / 2.4.7 |
| Python | 3.12 (functional tests) |

> ⚠ **Wallet compatibility:** a `wallet.dat` created with BDB 5.3 may not open in the official binaries, which are built with BDB 4.8.
> Release binaries are built with `depends/` (BDB 4.8.30); the build against the system libraries of the build server is used only for development and testing.

## Installing the dependencies

```bash
sudo apt-get install -y build-essential libtool autotools-dev automake pkg-config bsdmainutils python3 \
  libevent-dev libboost-dev libboost-system-dev libboost-filesystem-dev libboost-program-options-dev \
  libboost-thread-dev libboost-chrono-dev libboost-test-dev libssl-dev libdb5.3++-dev \
  libminiupnpc-dev libzmq3-dev autoconf-archive
```

The functional tests also need `python3-pyasyncore` (`asyncore` was removed in Python 3.12): `sudo apt-get install -y python3-pyasyncore`.

`autoconf-archive` is the source of the C++17 check macro (`ax_cxx_compile_stdcxx.m4` serial 15). It has already been copied into `build-aux/m4/`, so it is not strictly required for the build.

## Build

```bash
cd <repo>
./autogen.sh
./configure --with-incompatible-bdb --without-gui --disable-bench --enable-tests --with-tx
# With the GUI: sudo apt install qtbase5-dev qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler libqrencode-dev
#               ./configure --with-incompatible-bdb --with-gui=qt5 ...  → src/qt/aokchain-qt (build confirmed with Qt 5.15, 2026-11-11)
make -j8
```

- Outputs: `src/aokchaind`, `src/aokchain-cli`, `src/aokchain-tx`, `src/test/test_aokchain`, `src/test/test_aokchain_hash`, `src/.libs/libaokchainconsensus.so`
- The tests (`--enable-tests`) and `aokchain-tx` (`--with-tx`) are **off by default**, so the options must be given.
- A clean build takes about 5 to 6 minutes (on 8 cores).

### Caution: `--disable-shared` contamination
The end of `configure.ac` appends `--disable-shared`, which is meant for the secp256k1 sub-configure, to `ac_configure_args`.
Because of this, when `make` reruns automake and `config.status --recheck` is executed, **the main tree is switched to shared-disabled as well** (`libaokchainconsensus.so` disappears).
If `./libtool --features` shows `disable shared libraries`, run `./configure ...` again from the start.

## Tests

### Unit tests
```bash
./src/test/test_aokchain                         # all
./src/test/test_aokchain --run_test=token_tx_tests   # one suite
./src/test/test_aokchain --report_level=short
```
For the current state see 05-stability-testing.md (73 of 78 pass; the 5 failures are all in `bloom_tests`).

### Functional tests (these actually start regtest nodes)
```bash
python3 test/functional/test_runner.py -j8                  # default set
python3 test/functional/test_runner.py rpc_uptime.py        # a single test
python3 test/functional/test_runner.py -j8 --combinedlogslen=0 > /tmp/ft.log
```
- The first run creates a 200-block chain cache in `test/cache/`. If the cache gets corrupted, `rm -rf test/cache`.
- The unit tests and the functional tests use the same binaries, so **do not run `make` while the functional tests are running.**

### Minimum set for regression checks (always after a change)
```bash
make -j8 && ./src/test/test_aokchain --report_level=short
python3 test/functional/test_runner.py -j8 $(cat doc/aokchain/functional-baseline.txt)
```
`functional-baseline.txt` contains the list of functional tests that currently pass. When a test is fixed so that it passes, add it to this list.

## Examples for checking that the binaries work

```bash
./src/aokchain-tx -create nversion=2 time=1700000000     # → 0200000000f1536500000000000000
./src/test/test_aokchain_hash <80-byte header hex>         # blake2b block hash
nm -DC --undefined-only src/.libs/libaokchainconsensus.so | grep -v GLIBC   # there must be no undefined symbols
```

## depends build (for releases)

The packages other than the GUI ones (Boost 1.83, OpenSSL 3.0.15, libevent 2.1.12, ZeroMQ 4.3.5, miniupnpc 2.2.7, BDB 4.8.30) have been updated, and the build and tests were confirmed on the build server. Qt was updated to 5.15.10 and qrencode to 4.1.1 (the recipes and patches of Bitcoin Core v26.2 were carried over). **The Qt package was verified only for the Windows host (`x86_64-w64-mingw32`).** The Qt depends for Linux does not build because the xcb family of packages has not been updated.

```bash
make -C depends NO_QT=1 -j8          # about 15 minutes the first time. Sources are cached in depends/sources/
# build in a separate worktree so that it does not mix with a tree that has already been configured
git worktree add ../aokchain-depbuild HEAD && cd ../aokchain-depbuild
./autogen.sh
CONFIG_SITE=$PWD/../AokChain/depends/x86_64-pc-linux-gnu/share/config.site \
  ./configure --prefix=/ --without-gui --disable-bench --enable-tests --with-tx
make -j8
```
- The result links only libc and libstdc++ dynamically. It uses BDB 4.8, so it is compatible with existing `wallet.dat` files.
- As of 2026-11-10 there is a finished build in the `aokchain-depbuild` worktree next to the repository on the build server.

### Windows (x86_64) cross build

It builds from the `modernize` branch alone (the 3 commits with the build setup for Windows were merged into this branch on 2026-11-11). The Windows build tree on the build server is not a separate branch but a work tree that checks out a commit of `modernize` as it is: `git -C <winbuild-dir> checkout --detach <modernize commit>`, then `make`. When `configure.ac` changes, rerun the `CONFIG_SITE=… ./configure` below (a plain `make` loses the depends paths and cannot find BDB 4.8).

```bash
sudo apt-get install -y g++-mingw-w64-x86-64-posix nsis zip     # use the posix threads variant
make -C depends HOST=x86_64-w64-mingw32 -j8                     # about 30 minutes including Qt
./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site \
  ./configure --prefix=/ --disable-bench --disable-tests --with-gui=qt5 --with-tx
make -j8
x86_64-w64-mingw32-strip src/aokchaind.exe src/aokchain-cli.exe src/aokchain-tx.exe src/qt/aokchain-qt.exe
```
- The result is statically linked. `x86_64-w64-mingw32-objdump -p <exe> | grep "DLL Name"` must show only Windows system DLLs.
- Qt links directly against the OpenSSL of depends because of BIP70 payment requests (`-openssl-linked`).
- `depends/patches/qt/*.patch` is caught by `*.patch` in `.gitignore`, so use `git add -f` when adding them.

## Mainnet verification node (`<verify-dir>/`)

| Path | Purpose |
|---|---|
| `bin/`, `data/` | Live verification node connected to mainnet. It does not accept incoming connections (`listen=0`). RPC 33440. `bin/BUILT_FROM` records the commit it was built from |
| `bin-par8/`, `data-par8/` | Offline node that revalidates the whole chain without the network (`connect=0`). RPC 33450 |
| `scan_final.json` | Result of the history scan |

```bash
V=<verify-dir>
$V/bin/aokchain-cli -datadir=$V/data getblockchaininfo      # status
$V/bin/aokchain-cli -datadir=$V/data stop                   # stop
$V/bin/aokchaind -datadir=$V/data -daemon                   # start
python3 contrib/aokchain/history_scan.py --datadir=$V/data --out=/tmp/scan.json   # history scan (about 11 minutes)
```
- When consensus or the block acceptance path is changed, copy the new binaries into `bin/`, restart, and check that the node **keeps following the tip**. The `startingheight` of the peers and our `blocks` must be the same.
- If `InvalidChainFound` appears in the log, check with `getchaintips`. A `bad-cs-proofhash` on a side branch of length 1 is an invalid PoS block that is being broadcast on the network and is normal. If our tip has stopped, it is a regression.
- Do not use `submitblock` on a node whose reindex has not finished (the block files get overwritten).
