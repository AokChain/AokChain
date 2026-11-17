Mac OS X Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Release build on Apple silicon
------------------------------
This is how the macOS programs of a release are built: every library comes
from `depends`, Qt included, and nothing is taken from Homebrew or the system
except the compiler and the build tools. The programs run on macOS 11.0 and
later.

Install Xcode (or its command line tools) and, for the build tools only,
`git`, `pkg-config`, `python3`, `autoconf`, `automake` and `libtool`. Then,
in a clean checkout of the release tag:

    make -C depends HOST=aarch64-apple-darwin darwin_native_packages=
    ./autogen.sh
    CONFIG_SITE=$PWD/depends/aarch64-apple-darwin/share/config.site ./configure --prefix=/ --with-gui=qt5 --disable-gui-tests --disable-bench --enable-tests --with-tx
    make
    make appbundle

`darwin_native_packages=` leaves out the helpers that only a cross build from
Linux needs. `make appbundle` writes `AokChain-Qt.app`; because Qt is linked
in, the bundle refers to nothing outside the system and needs no
`macdeployqt`. `hdiutil create` makes the disk image from it. The node and the
command-line tools are `src/aokchaind`, `src/aokchain-cli`, `src/aokchain-tx`
and `src/aokchain-wallet`.

The unit tests run with `contrib/aokchain/unit-tests.sh`, the functional tests
with `test/functional/test_runner.py` (they need a Python that still has
`asyncore`, that is 3.11 or older).

A program that is given to other people has to be signed with a Developer ID
Application certificate and notarized, or Gatekeeper refuses it.

The sections below are the older way with Homebrew libraries. They are kept
for reference; current Homebrew versions of Boost need source changes that
the tree does not have.

Preparation
-----------
Install the OS X command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

Then install [Homebrew](https://brew.sh).

Dependencies
----------------------

    brew install automake berkeley-db4 libtool boost --c++11 miniupnpc openssl pkg-config protobuf python3 qt libevent

See [dependencies.md](dependencies.md) for a complete overview.

If you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG

    brew install librsvg

NOTE: Building with Qt4 is still supported, however, could result in a broken UI. Building with Qt5 is recommended.

NOTE: At this time it is highly recommended that developers wishing to compile the AokChain Core binaries **DO NOT** upgrade to 
OS X Mojave Beta 10.14.  Currently there is a compatibility issue with OS X Mojave, Command-Line-Tools 10.0.0 (clang), and 
Berkeley-db version 4.8.3.  Binaries compiled using this combination will crash with a segmentation-fault during initialization. 
Binaries compiled by previous versions will run on OS X Mojave with no-known issues.  It is possible to work-around this issue by 
upgrading Berkeley-db to version 18.1.25 or newer (currently 18.1.25 is the only known version to work).  To compile and run with 
newer versions of Berkeley-db it is recommended that Berkeley-db 4.8.3 be uninstalled and the latest version installed.  There are 
unknown wallet compatability ramifications to this solution so it is highly recommended that any local wallets be backed-up before
opening them using binaries compiled with this solution.

Use the following commands to compile a working version of AokChain Core on Mojave (assuming that the instructions in the section "Build 
AokChain Core" has already been followed).  Uninstall Berkeley-db 4.8.3, install the latest version, and _configure_ with the 
incompatible-bdb flag:

    brew remove berkeley-db@4
    brew install bekeley-db
    ./autogen.sh
    ./configure --with-incompatible-bdb
    make


Build AokChain Core
------------------------

1. Clone the aokchain source code and cd into `aokchain`

        git clone https://github.com/AokChain/AokChain
        cd AokChain

2.  Build aokchain-core:

    Configure and build the headless aokchain binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.

        ./autogen.sh
        ./configure
        make

3.  It is recommended to build and run the unit tests:

        make check

4.  You can also create a .dmg that contains the .app bundle (optional):

        make deploy

Running
-------

AokChain Core is now available at `./src/aokchaind`

Before running, it's recommended you create an RPC configuration file.

    echo -e "rpcuser=aokchainrpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/AokChain/aokchain.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/AokChain/aokchain.conf"

The first time you run aokchaind, it will start downloading the blockchain. This process could take several hours.

You can monitor the download process by looking at the debug.log file:

    tail -f $HOME/Library/Application\ Support/AokChain/debug.log

Other commands:
-------

    ./src/aokchaind -daemon # Starts the aokchain daemon.
    ./src/aokchain-cli --help # Outputs a list of command-line options.
    ./src/aokchain-cli help # Outputs a list of RPC commands when the daemon is running.

Using Qt Creator as IDE
------------------------
You can use Qt Creator as an IDE, for aokchain development.
Download and install the community edition of [Qt Creator](https://www.qt.io/download/).
Uncheck everything except Qt Creator during the installation process.

1. Make sure you installed everything through Homebrew mentioned above
2. Do a proper ./configure --enable-debug
3. In Qt Creator do "New Project" -> Import Project -> Import Existing Project
4. Enter "aokchain-qt" as project name, enter src/qt as location
5. Leave the file selection as it is
6. Confirm the "summary page"
7. In the "Projects" tab select "Manage Kits..."
8. Select the default "Desktop" kit and select "Clang (x86 64bit in /usr/bin)" as compiler
9. Select LLDB as debugger (you might need to set the path to your installation)
10. Start debugging with Qt Creator

Notes
-----

* The release build is made and tested on Apple silicon; the lowest supported version is macOS 11.0. The Homebrew instructions were last tested on OS X 10.8 through 10.14 on 64-bit Intel processors.

* Building with downloaded Qt binaries is not officially supported. 

* autoreconf (boost issue)
