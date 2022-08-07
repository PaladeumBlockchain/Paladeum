#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

PLDD=${PLDD:-$SRCDIR/paladeumd}
PLDCLI=${PLDCLI:-$SRCDIR/paladeum-cli}
PLDTX=${PLDTX:-$SRCDIR/paladeum-tx}
PLDQT=${PLDQT:-$SRCDIR/qt/paladeum-qt}

[ ! -x $PLDD ] && echo "$PLDD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
PLDVER=($($PLDCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for paladeumd if --version-string is not set,
# but has different outcomes for paladeum-qt and paladeum-cli.
echo "[COPYRIGHT]" > footer.h2m
$PLDD --version | sed -n '1!p' >> footer.h2m

for cmd in $PLDD $PLDCLI $PLDTX $PLDQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${PLDVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${PLDVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
