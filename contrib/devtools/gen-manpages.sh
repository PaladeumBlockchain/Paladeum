#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

YONAD=${YONAD:-$SRCDIR/yonad}
YONACLI=${YONACLI:-$SRCDIR/yona-cli}
YONATX=${YONATX:-$SRCDIR/yona-tx}
YONAQT=${YONAQT:-$SRCDIR/qt/yona-qt}

[ ! -x $YONAD ] && echo "$YONAD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
YONAVER=($($YONACLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for yonad if --version-string is not set,
# but has different outcomes for yona-qt and yona-cli.
echo "[COPYRIGHT]" > footer.h2m
$YONAD --version | sed -n '1!p' >> footer.h2m

for cmd in $YONAD $YONACLI $YONATX $YONAQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${YONAVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${YONAVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
