#!/usr/bin/env bash
# Install or upgrade an AokChain Core node on Linux (x86_64, glibc 2.34 or later:
# Ubuntu 22.04, Debian 12 and newer).
#
#   curl -fsSL <base url>/upgrade.sh | bash
#
# What it does:
#   1. downloads the release archive and checks its SHA256 against the value in this script
#   2. stops the running node (systemd unit "aokchaind" if there is one, else aokchain-cli stop)
#   3. keeps a copy of the old programs and of the wallet file
#   4. installs the new programs where the old ones are (default /usr/local/bin)
#   5. starts the node the way it was running and waits until it answers
#
# The data directory is used as it is: no reindex, no new download of the chain. An
# existing aokchain.conf is never changed; one is written only when there is none.
# The wallet is not encrypted, decrypted or otherwise changed.
#
# Settings (environment variables, all optional):
#   AOK_BASE      where the archive is downloaded from (default: where this script is published)
#   AOK_DATADIR   data directory (default: ~/.aokchain)
#   AOK_BINDIR    where to install (default: the directory of the installed aokchaind, else /usr/local/bin)
#   AOK_START=0   do not start the node after installing
#   AOK_REPAIR=0  do not run "repaircoins" after an upgrade from 3.0.x
#   AOK_SYSTEMD=1 on a machine without an aokchaind unit, install one (starts the node at boot)

set -euo pipefail

VERSION="@VERSION@"
ARCHIVE="aokchain-${VERSION}-linux-x86_64.tar.gz"
ARCHIVE_SHA256="@SHA256@"
DEFAULT_BASE="@BASE@"

main() {
    local base="${AOK_BASE:-$DEFAULT_BASE}"
    local datadir="${AOK_DATADIR:-$HOME/.aokchain}"
    local conf="$datadir/aokchain.conf"
    local start="${AOK_START:-1}"

    say()  { printf '\n== %s\n' "$*"; }
    fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

    # ---- requirements
    [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ] || fail "this archive is for Linux x86_64"
    [ "$(id -u)" != "0" ] || fail "run this as the user that runs the node, not as root (sudo is asked for when needed)"
    local tool
    for tool in curl tar sha256sum pgrep install; do
        command -v "$tool" >/dev/null || fail "'$tool' is needed"
    done
    local glibc
    glibc="$(ldd --version 2>/dev/null | head -n 1 | grep -o '[0-9]\+\.[0-9]\+$' || true)"
    if [ -n "$glibc" ] && [ "$(printf '%s\n2.34\n' "$glibc" | sort -V | head -n 1)" != "2.34" ]; then
        fail "glibc $glibc is too old; 2.34 or later is needed (Ubuntu 22.04, Debian 12 or newer)"
    fi

    # ---- where the programs go
    local bindir="${AOK_BINDIR:-}"
    if [ -z "$bindir" ]; then
        if command -v aokchaind >/dev/null; then
            bindir="$(dirname "$(readlink -f "$(command -v aokchaind)")")"
        else
            bindir="/usr/local/bin"
        fi
    fi
    local as_root=""
    if ! { [ -d "$bindir" ] && [ -w "$bindir" ]; }; then
        as_root="sudo"
    fi
    local old_version="none"
    if [ -x "$bindir/aokchaind" ]; then
        old_version="$("$bindir/aokchaind" --version 2>/dev/null | head -n 1 | grep -o 'v[0-9][^ ]*' || echo unknown)"
    fi
    local cli=("$bindir/aokchain-cli" "-datadir=$datadir")

    say "AokChain Core $VERSION"
    echo "programs:        $bindir (installed now: $old_version)"
    echo "data directory:  $datadir"

    # ---- download and check
    local work
    work="$(mktemp -d)"
    # (expanded now: "work" is gone when the trap runs)
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT
    say "Downloading $ARCHIVE"
    curl -fL --retry 3 -o "$work/$ARCHIVE" "$base/$ARCHIVE"
    echo "$ARCHIVE_SHA256  $work/$ARCHIVE" | sha256sum -c - >/dev/null || fail "the SHA256 of the download is not $ARCHIVE_SHA256"
    echo "SHA256 ok"
    tar -xzf "$work/$ARCHIVE" -C "$work"
    local new="$work/aokchain-${VERSION}-linux-x86_64"
    local new_version
    new_version="$("$new/aokchaind" --version | head -n 1 | grep -o 'v[0-9][^ ]*')" || fail "the new aokchaind does not run on this machine"
    echo "new programs:    $new_version"

    # ---- configuration: an existing file is left alone
    mkdir -p "$datadir"
    if [ -f "$conf" ]; then
        echo "configuration:   $conf (kept as it is)"
    else
        ( umask 077
          printf 'server=1\nrpcuser=aokrpc\nrpcpassword=%s\nrpcbind=127.0.0.1\nrpcallowip=127.0.0.1\n' \
              "$(head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n')" > "$conf" )
        echo "configuration:   $conf (written, there was none)"
    fi

    # ---- how the node runs
    local unit=""
    if command -v systemctl >/dev/null && systemctl cat aokchaind.service >/dev/null 2>&1; then
        unit="aokchaind.service"
    fi
    local was_running=0
    if pgrep -x aokchaind >/dev/null; then
        was_running=1
    fi

    # ---- stop
    if [ "$was_running" = 1 ]; then
        say "Stopping the node"
        if [ -n "$unit" ] && systemctl is-active --quiet "$unit"; then
            sudo systemctl stop "$unit"
        else
            "${cli[@]}" stop || fail "the node does not answer 'aokchain-cli stop'; stop it and run this again"
        fi
        local waited=0
        while pgrep -x aokchaind >/dev/null; do
            [ "$waited" -lt 600 ] || fail "the node is still running after 10 minutes"
            sleep 2; waited=$((waited + 2))
        done
        echo "stopped"
    fi

    # ---- keep the old programs and the wallet
    local stamp keep
    stamp="$(date +%Y%m%d-%H%M%S)"
    keep="$datadir/upgrade-backup/$stamp"
    ( umask 077; mkdir -p "$keep" )
    local f
    for f in aokchaind aokchain-cli aokchain-tx aokchain-wallet; do
        if [ -f "$bindir/$f" ]; then cp -p "$bindir/$f" "$keep/"; fi
    done
    if [ -f "$datadir/wallet.dat" ]; then cp -p "$datadir/wallet.dat" "$keep/"; fi
    if [ -d "$datadir/wallets" ]; then cp -a "$datadir/wallets" "$keep/"; fi
    if [ -n "$(ls -A "$keep")" ]; then
        echo "backup:          $keep (old programs and wallet)"
    else
        rmdir "$keep"; keep=""
        echo "backup:          nothing to keep (new installation)"
    fi

    # ---- install
    say "Installing"
    $as_root mkdir -p "$bindir"
    $as_root install -m 755 "$new/aokchaind" "$new/aokchain-cli" "$new/aokchain-tx" "$new/aokchain-wallet" "$bindir/"
    echo "installed $("$bindir/aokchaind" --version | head -n 1)"

    if [ -z "$unit" ] && [ "${AOK_SYSTEMD:-0}" = 1 ] && command -v systemctl >/dev/null; then
        say "Installing the systemd unit aokchaind.service"
        sudo tee /etc/systemd/system/aokchaind.service >/dev/null <<UNIT
[Unit]
Description=AokChain Core daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=forking
User=$(id -un)
Group=$(id -gn)
PIDFile=$datadir/aokchaind.pid
ExecStart=$bindir/aokchaind -daemon -datadir=$datadir -pid=$datadir/aokchaind.pid
ExecStop=$bindir/aokchain-cli -datadir=$datadir stop
Restart=on-failure
RestartSec=30
TimeoutStartSec=300
TimeoutStopSec=600

[Install]
WantedBy=multi-user.target
UNIT
        sudo systemctl daemon-reload
        sudo systemctl enable aokchaind.service >/dev/null
        unit="aokchaind.service"
    fi

    # ---- start
    if [ "$start" != 1 ]; then
        say "Not started (AOK_START=0)"
        return 0
    fi
    say "Starting the node"
    if [ -n "$unit" ]; then
        sudo systemctl start "$unit"
    else
        "$bindir/aokchaind" -daemon "-datadir=$datadir" >/dev/null
    fi
    local waited=0 blocks=""
    until blocks="$("${cli[@]}" getblockcount 2>/dev/null)"; do
        pgrep -x aokchaind >/dev/null || fail "the node stopped while starting; see $datadir/debug.log${keep:+. The old programs are in $keep}"
        [ "$waited" -lt 1800 ] || fail "the node does not answer after 30 minutes; see $datadir/debug.log"
        sleep 5; waited=$((waited + 5))
    done
    echo "running:         $("${cli[@]}" getnetworkinfo | grep '"subversion"' | tr -d ' ,')"
    echo "blocks:          $blocks"

    # ---- coins that 3.0.x left without a time (doc/aokchain/16-stake-coin-time.md)
    case "$old_version" in
        v3.*|v2.*|v1.*)
            if [ "${AOK_REPAIR:-1}" = 1 ]; then
                say "Checking the coins that 3.0.x stored without their time"
                local report
                if report="$("${cli[@]}" repaircoins true 2>&1)"; then
                    echo "$report" | grep -E '"scanned"|"damaged"' || true
                    if ! echo "$report" | grep -q '"damaged": 0'; then
                        "${cli[@]}" repaircoins | grep -E '"repaired"|"unreadable"' || true
                    fi
                else
                    echo "skipped: $report"
                fi
            fi
            ;;
    esac

    say "Done"
    echo "The node is synchronizing in the background:  $bindir/aokchain-cli getblockchaininfo"
    if [ -n "$keep" ]; then
        echo "To go back: stop the node, copy the programs from $keep to $bindir, start it."
    fi
}

main "$@"
