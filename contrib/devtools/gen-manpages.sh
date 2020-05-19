#!/usr/bin/env bash

export LC_ALL=C
TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
BUILDDIR=${BUILDDIR:-$TOPDIR}

BINDIR=${BINDIR:-$BUILDDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

AOKCHAIND=${AOKCHAIND:-$BINDIR/aokchaind}
AOKCHAINCLI=${AOKCHAINCLI:-$BINDIR/aokchain-cli}
AOKCHAINTX=${AOKCHAINTX:-$BINDIR/aokchain-tx}
WALLET_TOOL=${WALLET_TOOL:-$BINDIR/aokchain-wallet}
AOKCHAINQT=${AOKCHAINQT:-$BINDIR/qt/aokchain-qt}

[ ! -x $AOKCHAIND ] && echo "$AOKCHAIND not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
AOKVER=($($AOKCHAINCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for aokchaind if --version-string is not set,
# but has different outcomes for aokchain-qt and aokchain-cli.
echo "[COPYRIGHT]" > footer.h2m
$AOKCHAIND --version | sed -n '1!p' >> footer.h2m

for cmd in $AOKCHAIND $AOKCHAINCLI $AOKCHAINTX $WALLET_TOOL $AOKCHAINQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${AOKVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${AOKVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
