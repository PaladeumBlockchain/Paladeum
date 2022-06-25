#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

AKILAD=${AKILAD:-$SRCDIR/akilad}
AKILACLI=${AKILACLI:-$SRCDIR/akila-cli}
AKILATX=${AKILATX:-$SRCDIR/akila-tx}
AKILAQT=${AKILAQT:-$SRCDIR/qt/akila-qt}

[ ! -x $AKILAD ] && echo "$AKILAD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
AKILAVER=($($AKILACLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for akilad if --version-string is not set,
# but has different outcomes for akila-qt and akila-cli.
echo "[COPYRIGHT]" > footer.h2m
$AKILAD --version | sed -n '1!p' >> footer.h2m

for cmd in $AKILAD $AKILACLI $AKILATX $AKILAQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${AKILAVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${AKILAVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
