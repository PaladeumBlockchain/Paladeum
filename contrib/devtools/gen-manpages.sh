#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

PLBD=${PLBD:-$SRCDIR/paladeumd}
PLBCLI=${PLBCLI:-$SRCDIR/paladeum-cli}
PLBTX=${PLBTX:-$SRCDIR/paladeum-tx}
PLBQT=${PLBQT:-$SRCDIR/qt/paladeum-qt}

[ ! -x $PLBD ] && echo "$PLBD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
PLBVER=($($PLBCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for paladeumd if --version-string is not set,
# but has different outcomes for paladeum-qt and paladeum-cli.
echo "[COPYRIGHT]" > footer.h2m
$PLBD --version | sed -n '1!p' >> footer.h2m

for cmd in $PLBD $PLBCLI $PLBTX $PLBQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${PLBVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${PLBVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
