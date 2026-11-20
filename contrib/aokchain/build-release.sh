#!/usr/bin/env bash
# Build the release programs of AokChain Core for Linux, Windows and macOS on a Linux machine, and
# pack them into zip archives to publish with a GitHub release.
#
# usage: contrib/aokchain/build-release.sh [options] [platform ...]
#
# platforms (default: linux-x86_64 windows-x86_64 macos-x86_64 macos-arm64)
#   linux-x86_64     x86_64-linux-gnu      runs on glibc 2.35 or newer (Ubuntu 22.04, Debian 12, ...)
#   linux-aarch64    aarch64-linux-gnu     (the same)
#   windows-x86_64   x86_64-w64-mingw32    Windows 10 or newer
#   macos-x86_64     x86_64-apple-darwin   macOS 11 or newer
#   macos-arm64      aarch64-apple-darwin  (the same), Apple silicon
#
# options
#   --macos-sdk PATH  the macOS SDK, needed for the macOS platforms: a MacOSX*.sdk folder, or a
#                     .tar.gz / .tar.xz of one (see "macOS SDK" below)
#   --ref REF         the commit, tag or branch to build (default: HEAD)
#   --worktree        build the files as they are now in the working tree (on top of REF): tracked
#                     files and new ones git does not ignore. Without it, uncommitted changes are left out
#   --work DIR        the folder everything goes into (default: ~/aokchain-release): the archives
#                     (<version>/), the depends cache (cache/, so the next release only builds what
#                     changed), the logs (logs/) and, while it runs, the build trees (build/)
#   --jobs N          parallel jobs (default: all processors)
#   --keep            keep the build trees
#
# Everything is built in Docker containers, from two images the script makes the first time it runs:
# Ubuntu 22.04 for Linux (so the programs run on older systems as well) and Ubuntu 24.04 with clang 18,
# lld and MinGW-w64 for Windows and macOS. The machine needs docker, git and about 30 GB of disk; the
# first run takes about an hour on 8 cores (most of it Qt, once per platform). DOCKER="sudo docker"
# when your user is not in the docker group; DOCKER=podman works too.
#
# The result, for version V, in <work>/V/: aokchain-V-<platform>.zip for every platform, also
# aokchain-V-linux-x86_64.tar.gz (contrib/aokchain/upgrade.sh downloads that one), and SHA256SUMS.
# An archive holds the folder aokchain-V-<platform> with the programs (aokchaind, aokchain-cli,
# aokchain-tx, aokchain-wallet, aokchain-qt); for macOS AokChain-Qt.app instead of aokchain-qt.
#
# macOS SDK: Apple's license does not allow passing the SDK around, so it is neither in the tree nor
# downloaded. Copy one from a Mac with the Command Line Tools (14.4 is what this was tested with):
#     COPYFILE_DISABLE=1 tar --no-xattrs --no-fflags --no-mac-metadata \
#         -C /Library/Developer/CommandLineTools/SDKs -czf MacOSX14.4.sdk.tar.gz MacOSX14.4.sdk
# or extract one from Xcode (contrib/macdeploy/README.md). The macOS programs are not signed with a
# Developer ID nor notarized: macOS asks before it opens them the first time (right click > Open).
# The arm64 programs carry an ad hoc signature, without which Apple silicon does not start a program.

set -euo pipefail

say() { printf '\n==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DOCKER=${DOCKER:-docker}
REF=HEAD
WORKTREE=0
WORK=$HOME/aokchain-release
JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN)
KEEP=0
SDK=
PLATFORMS=()

while [ $# -gt 0 ]; do
    case $1 in --*=*) set -- "${1%%=*}" "${1#*=}" "${@:2}" ;; esac
    case $1 in
        --macos-sdk) SDK=${2:?}; shift 2 ;;
        --ref) REF=${2:?}; shift 2 ;;
        --worktree) WORKTREE=1; shift ;;
        --work) WORK=${2:?}; shift 2 ;;
        --jobs) JOBS=${2:?}; shift 2 ;;
        --keep) KEEP=1; shift ;;
        -h|--help) sed -n '2,/^$/{s/^# \{0,1\}//;p;}' "$0"; exit 0 ;;
        -*) die "unknown option $1 (see --help)" ;;
        *) PLATFORMS+=("$1"); shift ;;
    esac
done
[ ${#PLATFORMS[@]} -gt 0 ] || PLATFORMS=(linux-x86_64 windows-x86_64 macos-x86_64 macos-arm64)

host_of() {
    case $1 in
        linux-x86_64) echo x86_64-linux-gnu ;;
        linux-aarch64) echo aarch64-linux-gnu ;;
        windows-x86_64) echo x86_64-w64-mingw32 ;;
        macos-x86_64) echo x86_64-apple-darwin ;;
        macos-arm64) echo aarch64-apple-darwin ;;
        *) return 1 ;;
    esac
}

need_sdk=0
for p in "${PLATFORMS[@]}"; do
    host_of "$p" >/dev/null || die "unknown platform $p (see --help)"
    case $p in macos-*) need_sdk=1 ;; esac
done

# ---- checks
command -v git >/dev/null || die "git is not installed"
$DOCKER info >/dev/null 2>&1 || die "'$DOCKER info' fails. Install docker (sudo apt install docker.io) and add yourself to the docker group (sudo usermod -aG docker \$USER, then log in again), or run with DOCKER=\"sudo docker\""
COMMIT=$(git -C "$REPO" rev-parse --verify "$REF^{commit}") || die "$REF is not a commit"
EPOCH=$(git -C "$REPO" log -1 --format=%ct "$COMMIT")
if [ $WORKTREE = 0 ] && [ -n "$(git -C "$REPO" status --porcelain --untracked-files=no)" ]; then
    echo "note: the working tree has uncommitted changes; they are not built (--worktree builds them)"
fi
if [ $need_sdk = 1 ] && [ -z "$SDK" ]; then
    die "the macOS platforms need --macos-sdk (see --help for how to get the SDK)"
fi
case $WORK in /*) ;; *) WORK=$PWD/$WORK ;; esac
mkdir -p "$WORK"/{cache/sources,cache/built,build,stage,logs}

# ---- macOS SDK: copied to <work>/sdk/current, which the containers see as /work/sdk/current
SDK_VERSION=
if [ $need_sdk = 1 ]; then
    say "macOS SDK from $SDK"
    rm -rf "$WORK/sdk"
    mkdir -p "$WORK/sdk/unpack"
    if [ -d "$SDK" ]; then
        cp -a "$SDK/." "$WORK/sdk/unpack/"
    elif [ -f "$SDK" ]; then
        # (an archive made by the tar of macOS has headers GNU tar does not know; they carry nothing needed)
        tar --warning=no-unknown-keyword -xf "$SDK" -C "$WORK/sdk/unpack"
    else
        die "$SDK is neither a folder nor a file"
    fi
    settings=$(find "$WORK/sdk/unpack" -maxdepth 3 -name SDKSettings.json | head -n 1)
    [ -n "$settings" ] || die "no SDKSettings.json in $SDK: not a macOS SDK"
    mv "$(dirname "$settings")" "$WORK/sdk/current"
    rm -rf "$WORK/sdk/unpack"
    [ -f "$WORK/sdk/current/usr/include/c++/v1/vector" ] || die "the SDK has no libc++ headers (usr/include/c++/v1)"
    SDK_VERSION=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["Version"])' "$WORK/sdk/current/SDKSettings.json") \
        || die "cannot read the version of the SDK from its SDKSettings.json"
    echo "SDK version $SDK_VERSION"
fi

# ---- the images
IMAGE_LINUX='FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf automake bison bsdmainutils bzip2 ca-certificates curl g++ git libtool make patch perl \
      pkg-config python3 xz-utils zip \
      g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
 && rm -rf /var/lib/apt/lists/*'

IMAGE_CROSS='FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf automake bison bsdmainutils bzip2 ca-certificates curl g++ git libtool make patch perl \
      pkg-config python3 xz-utils zip \
      g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 clang lld llvm \
 && update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
 && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix \
 && rm -rf /var/lib/apt/lists/*'

image() { # name, Dockerfile: builds the image unless one of this Dockerfile exists; prints its tag
    local tag
    tag="$1:$(printf '%s' "$2" | sha256sum | cut -c1-12)"
    if ! $DOCKER image inspect "$tag" >/dev/null 2>&1; then
        say "Docker image $tag" >&2
        printf '%s\n' "$2" | $DOCKER build -t "$tag" - >&2
    fi
    echo "$tag"
}

# ---- what runs in a container: build one platform and pack it
cat > "$WORK/.build-platform.sh" <<'INNER'
#!/usr/bin/env bash
set -euo pipefail
platform=$1 host=$2 jobs=$3 sdk_version=$4 epoch=$5
cd "/work/build/$platform"

make_args=(HOST="$host" SOURCES_PATH=/work/cache/sources BASE_CACHE=/work/cache/built)
if [ -n "$sdk_version" ]; then
    # (the salt keeps the cached packages of one SDK apart from those of another)
    make_args+=(OSX_SDK=/work/sdk/current OSX_SDK_VERSION="$sdk_version" HOST_ID_SALT="macos-sdk-$sdk_version")
fi
echo "--- depends for $host"
make -C depends -j"$jobs" "${make_args[@]}"

echo "--- configure"
./autogen.sh
# (no libaokchainconsensus: the archives have only the programs, and for macOS libtool would link the
# shared library without -fuse-ld=lld, which it does not pass on)
conf=(--prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking
      --disable-bench --disable-tests --disable-gui-tests --with-gui=qt5 --with-tx --without-libs)
case $host in
    *linux*) conf+=(--enable-reduce-exports LDFLAGS=-static-libstdc++) ;;
    *darwin*) conf+=(--enable-reduce-exports) ;;
esac
CONFIG_SITE="$PWD/depends/$host/share/config.site" ./configure "${conf[@]}"

echo "--- make"
make -j"$jobs"

echo "--- pack"
version=$(sed -n 's/^PACKAGE_VERSION = //p' Makefile)
name=aokchain-$version-$platform
stage=/work/stage/$platform
rm -rf "$stage"
mkdir -p "$stage/$name"
make install-strip DESTDIR="$stage/root" >/dev/null
install -m 755 "$stage"/root/bin/aokchain* "$stage/$name/"
install -m 644 COPYING "$stage/$name/"
case $host in
    *darwin*)
        make appbundle >/dev/null
        cp -pR AokChain-Qt.app "$stage/$name/"
        rm -f "$stage/$name/aokchain-qt"
        ;;
esac
rm -rf "$stage/root"
# (the time of the commit on every file: the same tree gives the same archive)
find "$stage/$name" -exec touch -h -d "@$epoch" {} +
cd "$stage"
find "$name" | LC_ALL=C sort | zip -X -q -y -@ "$name.zip"
case $platform in
    linux-x86_64)
        tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" -cf - "$name" | gzip -9n > "$name.tar.gz"
        ;;
esac

echo "--- check"
ls -l "$name"
case $host in
    *linux*)
        echo "newest glibc symbol: $(objdump -T "$name"/aokchain* | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -n 1)"
        if [ "$host" = "$(uname -m)-linux-gnu" ]; then "$name/aokchaind" -version | head -n 1; fi
        ;;
    *mingw32*)
        echo "DLLs of aokchain-qt.exe: $(x86_64-w64-mingw32-objdump -p "$name/aokchain-qt.exe" | sed -n 's/.*DLL Name: //p' | tr '\n' ' ')"
        ;;
    *darwin*)
        bindir=$(llvm-config --bindir)
        "$bindir/llvm-otool" -L "$name/AokChain-Qt.app/Contents/MacOS/AokChain-Qt"
        if "$bindir/llvm-otool" -l "$name/aokchaind" | grep -q LC_CODE_SIGNATURE; then echo "aokchaind: signed (ad hoc)"; else echo "aokchaind: no signature"; fi
        ;;
esac
echo "$name.zip done"
INNER

# ---- build
IMG_LINUX= IMG_CROSS=
userns=()
case $DOCKER in *podman*) userns=(--userns=keep-id) ;; esac
VERSION=
failed=()
for platform in "${PLATFORMS[@]}"; do
    host=$(host_of "$platform")
    case $platform in
        linux-*) [ -n "$IMG_LINUX" ] || IMG_LINUX=$(image aokchain-release-linux "$IMAGE_LINUX"); img=$IMG_LINUX ;;
        *) [ -n "$IMG_CROSS" ] || IMG_CROSS=$(image aokchain-release-cross "$IMAGE_CROSS"); img=$IMG_CROSS ;;
    esac
    sdk_version=
    case $platform in macos-*) sdk_version=$SDK_VERSION ;; esac

    say "$platform ($host) from $(git -C "$REPO" rev-parse --short "$COMMIT")$([ $WORKTREE = 1 ] && echo ' + working tree')"
    tree=$WORK/build/$platform
    rm -rf "$tree"
    git clone -q "$REPO" "$tree"
    git -C "$tree" -c advice.detachedHead=false checkout -q --detach "$COMMIT"
    if [ $WORKTREE = 1 ]; then
        (cd "$REPO" && git ls-files -z --cached --others --exclude-standard | tar --null --ignore-failed-read -T - -cf - 2>/dev/null) | tar -xf - -C "$tree"
        (cd "$REPO" && git ls-files -z --deleted) | (cd "$tree" && xargs -0 -r rm -f --)
    fi

    log=$WORK/logs/$platform.log
    echo "log: $log"
    if $DOCKER run --rm --init --network host -u "$(id -u):$(id -g)" "${userns[@]}" -e HOME=/tmp \
           -v "$WORK:/work" "$img" bash /work/.build-platform.sh "$platform" "$host" "$JOBS" "$sdk_version" "$EPOCH" \
           > "$log" 2>&1; then
        sed -n '/^--- check/,$p' "$log" | tail -n +2
        name=$(cd "$WORK/stage/$platform" && ls -- *.zip | sed 's/\.zip$//')
        VERSION=$(echo "$name" | sed "s/^aokchain-\(.*\)-$platform\$/\1/")
        mkdir -p "$WORK/$VERSION"
        mv "$WORK/stage/$platform/$name".* "$WORK/$VERSION/"
        rm -rf "$WORK/stage/$platform"
        [ $KEEP = 1 ] || rm -rf "$tree"
    else
        echo "FAILED; the end of $log:"
        tail -n 30 "$log"
        failed+=("$platform")
    fi
done

if [ -n "$VERSION" ]; then
    (cd "$WORK/$VERSION" && rm -f SHA256SUMS && sha256sum -- *.zip *.tar.gz 2>/dev/null > SHA256SUMS)
    say "Archives in $WORK/$VERSION"
    (cd "$WORK/$VERSION" && ls -l && echo && cat SHA256SUMS)
fi
if [ ${#failed[@]} -gt 0 ]; then
    say "Failed: ${failed[*]} (logs in $WORK/logs)"
    exit 1
fi
