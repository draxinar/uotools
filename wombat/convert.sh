#!/bin/bash
#
# Batch decode/encode a directory of Wombat scripts.
#
# Decoding produces source text from .m bytecode. Encoding always uses
# a reference directory (-r) so the output is byte-identical to the
# originals: every input file in srcdir must have a matching file in
# refdir, otherwise the run fails.
#
# Usage:
#   ./convert.sh -d [-s sdb.txt] srcdir dstdir
#   ./convert.sh -c [-s sdb.txt] -r refdir srcdir dstdir
#   ./convert.sh -f [-s sdb.txt] srcdir dstdir

set -e

WOMBAT="$(dirname "$0")/wombat"
MODE=""
SDB=""
REFDIR=""

usage() {
	local out=${1:-/dev/stderr}
	local code=${2:-1}
	{
		echo "usage: $0 -d [-s sdb.txt] srcdir dstdir"
		echo "       $0 -c [-s sdb.txt] -r refdir srcdir dstdir"
		echo "       $0 -f [-s sdb.txt] srcdir dstdir"
		echo
		echo "  -d        decode every srcdir/*.m to dstdir/*.m"
		echo "  -c        encode every srcdir/*.m to dstdir/*.m"
		echo "            (always uses -r; every file in srcdir must"
		echo "             have a matching file in refdir)"
		echo "  -f        format every srcdir/*.m to dstdir/*.m"
		echo "            (canonical layout, comments preserved)"
		echo "  -s sdb    path to sdb.txt (defaults to srcdir/sdb.txt"
		echo "            for -d or refdir/sdb.txt for -c)"
		echo "  -r refdir reference directory (required for -c)"
		echo "  -h        show this help and exit"
	} > "$out"
	exit "$code"
}

while [ $# -gt 0 ]; do
	case "$1" in
	-d) MODE="-d"; shift ;;
	-c) MODE="-c"; shift ;;
	-f) MODE="-f"; shift ;;
	-s) SDB="$2"; shift 2 ;;
	-r) REFDIR="$2"; shift 2 ;;
	-h) usage /dev/stdout 0 ;;
	*)  break ;;
	esac
done

if [ -z "$MODE" ] || [ $# -ne 2 ]; then
	usage /dev/stderr 1
fi

SRCDIR="$1"
DSTDIR="$2"

if [ ! -d "$SRCDIR" ]; then
	echo "error: source directory not found: $SRCDIR" >&2
	exit 1
fi

if [ "$MODE" = "-c" ]; then
	if [ -z "$REFDIR" ]; then
		echo "error: -c requires -r refdir (encode always preserves variants)" >&2
		usage /dev/stderr 1
	fi
	if [ ! -d "$REFDIR" ]; then
		echo "error: reference directory not found: $REFDIR" >&2
		exit 1
	fi
fi

# Default SDB path
if [ -z "$SDB" ]; then
	if { [ "$MODE" = "-d" ] || [ "$MODE" = "-f" ]; } && [ -f "$SRCDIR/sdb.txt" ]; then
		SDB="$SRCDIR/sdb.txt"
	elif [ "$MODE" = "-c" ] && [ -f "$REFDIR/sdb.txt" ]; then
		SDB="$REFDIR/sdb.txt"
	elif [ -f "sdb.txt" ]; then
		SDB="sdb.txt"
	else
		echo "error: cannot find sdb.txt, use -s" >&2
		exit 1
	fi
fi

mkdir -p "$DSTDIR"

if [ "$MODE" = "-d" ]; then
	echo "decoding $SRCDIR -> $DSTDIR (sdb: $SDB)"
elif [ "$MODE" = "-f" ]; then
	echo "formatting $SRCDIR -> $DSTDIR (sdb: $SDB)"
else
	echo "encoding $SRCDIR -> $DSTDIR (sdb: $SDB, ref: $REFDIR)"
fi

ok=0
fail=0
total=0

for f in "$SRCDIR"/*.m; do
	[ -f "$f" ] || continue
	base="$(basename "$f")"
	total=$((total + 1))

	if [ "$MODE" = "-c" ]; then
		if [ ! -f "$REFDIR/$base" ]; then
			echo "  FAIL: $base (no reference in $REFDIR)" >&2
			fail=$((fail + 1))
			continue
		fi
		if "$WOMBAT" -c -s "$SDB" -r "$REFDIR/$base" "$f" "$DSTDIR/$base"; then
			ok=$((ok + 1))
		else
			echo "  FAIL: $base" >&2
			fail=$((fail + 1))
		fi
	else
		if "$WOMBAT" "$MODE" -s "$SDB" "$f" "$DSTDIR/$base"; then
			ok=$((ok + 1))
		else
			echo "  FAIL: $base" >&2
			fail=$((fail + 1))
		fi
	fi
done

echo "done: $ok/$total ok, $fail failed"
[ "$fail" -eq 0 ]
