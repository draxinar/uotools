#!/bin/bash
#
# Test encoding and decoding round-trip on a whole directory.
#
# Usage: ./test.sh [-h] [scriptdir] [sdb.txt] [refdir]
#
# Downloads test data from https://github.com/draxinar/uodemo on first
# run if testdata/ is missing. The actual work is then a bulk
# decode -> encode-with-ref -> decode -> encode-with-ref pipeline run
# via ./convert.sh, followed by per-file sha1/diff/cmp checks against
# the originals and a no-reference fixpoint loop that exercises the
# default-variant encode path directly through the wombat binary.
#
# Reports counts for round-trip / bin-sha1 / nr-src / nr-bin /
# sdb-sha1 / ref-match; exits non-zero if any invariant fails.

set -e

case "$1" in
-h|--help)
	sed -n '2,15p' "$0" | sed 's/^# \?//'
	exit 0
	;;
esac

WOMBAT="$(dirname "$0")/wombat"
CONVERT="$(dirname "$0")/convert.sh"
TESTDATA="$(dirname "$0")/testdata"
REPO_URL="https://github.com/draxinar/uodemo"

# Download test data if missing
download_testdata() {
	echo "=== Downloading test data ==="
	echo "  repo: $REPO_URL"

	local tmpdir
	tmpdir=$(mktemp -d "/tmp/wombat-dl.XXXXXX")
	trap 'rm -rf "$tmpdir"' RETURN

	if command -v git >/dev/null 2>&1; then
		git clone --depth 1 --filter=blob:none --sparse \
			"$REPO_URL" "$tmpdir/uodemo" 2>&1
		(cd "$tmpdir/uodemo" && git sparse-checkout set rundir/scripts rundir/scripts.wombat) 2>&1
	else
		echo "error: git is required to download test data" >&2
		exit 1
	fi

	mkdir -p "$TESTDATA"
	cp -r "$tmpdir/uodemo/rundir/scripts" "$TESTDATA/scripts"
	cp -r "$tmpdir/uodemo/rundir/scripts.wombat" "$TESTDATA/scripts.wombat"

	echo "  downloaded to $TESTDATA"
	echo
}

if [ ! -d "$TESTDATA/scripts" ] || [ ! -f "$TESTDATA/scripts/sdb.txt" ]; then
	download_testdata
fi

SCRIPTDIR="${1:-$TESTDATA/scripts}"
SDB="${2:-$SCRIPTDIR/sdb.txt}"
REFDIR="${3:-$TESTDATA/scripts.wombat}"

TMPDIR=$(mktemp -d "/tmp/wombat-test.XXXXXX")
trap 'rm -rf "$TMPDIR"' EXIT

mkdir -p "$TMPDIR/decoded1" "$TMPDIR/encoded" "$TMPDIR/decoded2"
mkdir -p "$TMPDIR/nr_bin1" "$TMPDIR/nr_src2" "$TMPDIR/nr_bin2"

if [ ! -f "$SDB" ]; then
	echo "error: sdb not found: $SDB" >&2
	exit 1
fi

if [ ! -d "$SCRIPTDIR" ]; then
	echo "error: script dir not found: $SCRIPTDIR" >&2
	exit 1
fi

echo "=== Wombat round-trip test ==="
echo "  scripts: $SCRIPTDIR"
echo "  sdb:     $SDB"
echo "  ref:     $REFDIR"
echo

sdb_sha_before=$(sha1sum "$SDB" | cut -d' ' -f1)

# Bulk operations via convert.sh. Each call must succeed end-to-end
# (convert.sh exits non-zero on any failure, set -e will trip it).
"$CONVERT" -d -s "$SDB" "$SCRIPTDIR"      "$TMPDIR/decoded1" >/dev/null
"$CONVERT" -c -s "$SDB" -r "$SCRIPTDIR"   "$TMPDIR/decoded1" "$TMPDIR/encoded" >/dev/null
"$CONVERT" -d -s "$SDB" "$TMPDIR/encoded" "$TMPDIR/decoded2" >/dev/null

pass=0
fail=0
total=0
ref_match=0
ref_total=0
bin_match=0
bin_mismatch=0
nr_src_match=0
nr_src_fail=0
nr_bin_match=0
nr_bin_fail=0

# Per-file checks on the ref-based outputs. These only sha/diff/cmp;
# no wombat invocation, so the SDB is not touched here.
for f in "$SCRIPTDIR"/*.m; do
	[ -f "$f" ] || continue
	base="$(basename "$f")"
	total=$((total + 1))

	# bin-sha1: re-encoded matches original
	sha_orig=$(sha1sum "$f" | cut -d' ' -f1)
	sha_enc=$(sha1sum "$TMPDIR/encoded/$base" | cut -d' ' -f1)
	if [ "$sha_orig" = "$sha_enc" ]; then
		bin_match=$((bin_match + 1))
	else
		echo "FAIL [bin-sha1]: $base  orig=$sha_orig enc=$sha_enc"
		bin_mismatch=$((bin_mismatch + 1))
	fi

	# round-trip: decoded1 vs decoded2
	if diff -q "$TMPDIR/decoded1/$base" "$TMPDIR/decoded2/$base" >/dev/null 2>&1; then
		pass=$((pass + 1))
	else
		echo "FAIL [diff]:    $base"
		diff -u "$TMPDIR/decoded1/$base" "$TMPDIR/decoded2/$base" | head -20
		fail=$((fail + 1))
	fi

	# ref match: decoded1 vs human-readable reference
	if [ -d "$REFDIR" ] && [ -f "$REFDIR/$base" ]; then
		ref_total=$((ref_total + 1))
		if diff -q "$TMPDIR/decoded1/$base" "$REFDIR/$base" >/dev/null 2>&1; then
			ref_match=$((ref_match + 1))
		fi
	fi
done

# At this point only the ref-based codec path has run. The SDB must
# be untouched: ref-based encode looks up identifiers in the existing
# DB and never appends.
sdb_sha_after_ref=$(sha1sum "$SDB" | cut -d' ' -f1)
sdb_ok=0
if [ "$sdb_sha_before" = "$sdb_sha_after_ref" ]; then
	sdb_ok=1
else
	echo "FAIL [sdb-sha1]: sdb.txt modified by ref-based codec"
	echo "  before=$sdb_sha_before after=$sdb_sha_after_ref"
fi

# No-reference fixpoint loop (direct wombat calls; convert.sh
# intentionally doesn't expose this mode). Without -r the encoder
# may append new identifiers to the SDB - that's expected, so the
# sdb-sha1 invariant above only covers the ref-based portion.
#   src1 (decoded1) -> bin1 -> src2 -> bin2
#   verify sha1(src1) == sha1(src2), sha1(bin1) == sha1(bin2)
for f in "$SCRIPTDIR"/*.m; do
	[ -f "$f" ] || continue
	base="$(basename "$f")"

	if "$WOMBAT" -c -s "$SDB" "$TMPDIR/decoded1/$base" "$TMPDIR/nr_bin1/$base" 2>/dev/null &&
	   "$WOMBAT" -d -s "$SDB" "$TMPDIR/nr_bin1/$base" "$TMPDIR/nr_src2/$base" 2>/dev/null; then
		sha_s1=$(sha1sum "$TMPDIR/decoded1/$base" | cut -d' ' -f1)
		sha_s2=$(sha1sum "$TMPDIR/nr_src2/$base" | cut -d' ' -f1)
		if [ "$sha_s1" = "$sha_s2" ]; then
			nr_src_match=$((nr_src_match + 1))
		else
			echo "FAIL [nr-src]: $base"
			nr_src_fail=$((nr_src_fail + 1))
		fi

		if "$WOMBAT" -c -s "$SDB" "$TMPDIR/nr_src2/$base" "$TMPDIR/nr_bin2/$base" 2>/dev/null; then
			sha_b1=$(sha1sum "$TMPDIR/nr_bin1/$base" | cut -d' ' -f1)
			sha_b2=$(sha1sum "$TMPDIR/nr_bin2/$base" | cut -d' ' -f1)
			if [ "$sha_b1" = "$sha_b2" ]; then
				nr_bin_match=$((nr_bin_match + 1))
			else
				echo "FAIL [nr-bin]: $base"
				nr_bin_fail=$((nr_bin_fail + 1))
			fi
		fi
	fi
done

# Comment-stripping subtest (self-contained; uses a private SDB copy so the
# canonical sdb.txt and the testdata fixtures are untouched). Proves the
# encoder accepts C-style // and /* */ comments and strips them: the bytecode
# for a commented script must be byte-identical to the same script without
# comments. The URL string literal and the real "a / b" division exercise the
# no-regression cases (// inside a string, lone / as division).
cmt_ok=1
cmt_sdb="$TMPDIR/cmt_sdb.txt"
cp "$SDB" "$cmt_sdb"

cat > "$TMPDIR/cmt_plain.m" <<'EOF'
inherits itemmanip;

trigger use {
	int a = 0x0A;
	int b = 0x02;
	int c = a / b;
	webBrowse(this, "http://www.owo.com/");
	if (c > 0x00) {
		barkTo(this, user, "ok");
	}
	return(0x00);
}
EOF

cat > "$TMPDIR/cmt_commented.m" <<'EOF'
// top-of-file line comment
inherits itemmanip; // trailing line comment

/* a single-line block comment */
trigger use {
	/* a block comment
	   spanning multiple lines */
	int a = 0x0A;
	int b = 0x02;
	int c = a /* inline block between tokens */ / b;
	webBrowse(this, "http://www.owo.com/"); // // not a comment in the string
	if (c > 0x00) {
		barkTo(this, user, "ok");
	}
	return(0x00);
}
EOF

if "$WOMBAT" -c -s "$cmt_sdb" "$TMPDIR/cmt_plain.m" "$TMPDIR/cmt_plain.bin" 2>/dev/null &&
   "$WOMBAT" -c -s "$cmt_sdb" "$TMPDIR/cmt_commented.m" "$TMPDIR/cmt_commented.bin" 2>/dev/null; then
	sha_plain=$(sha1sum "$TMPDIR/cmt_plain.bin" | cut -d' ' -f1)
	sha_cmt=$(sha1sum "$TMPDIR/cmt_commented.bin" | cut -d' ' -f1)
	if [ "$sha_plain" != "$sha_cmt" ]; then
		echo "FAIL [comments]: commented bytecode differs from plain"
		echo "  plain=$sha_plain commented=$sha_cmt"
		cmt_ok=0
	fi
else
	echo "FAIL [comments]: encode failed"
	cmt_ok=0
fi

# Formatter (-f) subtest. Self-contained; uses a private SDB copy so the
# canonical sdb.txt and the fixtures stay untouched. Proves three things:
# -f is a no-op on canonical (comment-free) source, it preserves comments
# and is idempotent, and it never perturbs the compiled bytecode.
fmt_ok=1
fmt_sdb="$TMPDIR/fmt_sdb.txt"
cp "$SDB" "$fmt_sdb"

# (a) no-op on canonical: every decoded1 file is decode(binary), i.e.
# already canonical, so -f must reproduce it byte-for-byte.
fmt_noop_fail=0
for f in "$TMPDIR/decoded1"/*.m; do
	[ -f "$f" ] || continue
	base="$(basename "$f")"
	if "$WOMBAT" -f -s "$fmt_sdb" "$f" "$TMPDIR/fmt_noop.m" 2>/dev/null; then
		if ! cmp -s "$TMPDIR/fmt_noop.m" "$f"; then
			fmt_noop_fail=$((fmt_noop_fail + 1))
			if [ "$fmt_noop_fail" -eq 1 ]; then
				echo "FAIL [format-canon]: $base"
				diff -u "$f" "$TMPDIR/fmt_noop.m" | head -20
			fi
		fi
	else
		fmt_noop_fail=$((fmt_noop_fail + 1))
	fi
done
if [ "$fmt_noop_fail" -ne 0 ]; then
	echo "  format-canon failures: $fmt_noop_fail"
	fmt_ok=0
fi

# (b) comment preservation + idempotence on a messy commented source.
cat > "$TMPDIR/fmt_messy.m" <<'EOF'
// top-of-file line comment
inherits   itemmanip;// trailing line comment
/* a single-line block comment */
trigger use{
/* a block comment
   spanning multiple lines */
int a=10;
   int b =0x2;
int c = a /* inline block between tokens */ / b;
webBrowse( this,"http://www.owo.com/" );// // not a comment in the string
if(c>0){barkTo(this,user,"ok");}
return(0);
}
EOF
if "$WOMBAT" -f -s "$fmt_sdb" "$TMPDIR/fmt_messy.m" "$TMPDIR/fmt_1.m" 2>/dev/null &&
   "$WOMBAT" -f -s "$fmt_sdb" "$TMPDIR/fmt_1.m" "$TMPDIR/fmt_2.m" 2>/dev/null; then
	if ! cmp -s "$TMPDIR/fmt_1.m" "$TMPDIR/fmt_2.m"; then
		echo "FAIL [format-idem]: -f is not idempotent on commented source"
		diff -u "$TMPDIR/fmt_1.m" "$TMPDIR/fmt_2.m" | head -20
		fmt_ok=0
	fi
	for marker in "// top-of-file line comment" "// trailing line comment" \
	              "/* a single-line block comment */" "spanning multiple lines" \
	              "// // not a comment in the string"; do
		if ! grep -qF "$marker" "$TMPDIR/fmt_1.m"; then
			echo "FAIL [format-comments]: lost comment: $marker"
			fmt_ok=0
		fi
	done
else
	echo "FAIL [format-idem]: -f failed on commented source"
	fmt_ok=0
fi

# (c) bytecode invariance: encoding the messy source and its formatted form
# must produce identical bytecode (comments and layout never reach bytecode).
cp "$SDB" "$TMPDIR/fmt_enc_a.txt"
cp "$SDB" "$TMPDIR/fmt_enc_b.txt"
if "$WOMBAT" -c -s "$TMPDIR/fmt_enc_a.txt" "$TMPDIR/fmt_messy.m" "$TMPDIR/fmt_messy.bin" 2>/dev/null &&
   "$WOMBAT" -c -s "$TMPDIR/fmt_enc_b.txt" "$TMPDIR/fmt_1.m" "$TMPDIR/fmt_fmt.bin" 2>/dev/null; then
	if ! cmp -s "$TMPDIR/fmt_messy.bin" "$TMPDIR/fmt_fmt.bin"; then
		echo "FAIL [format-bin]: formatted bytecode differs from source"
		fmt_ok=0
	fi
else
	echo "FAIL [format-bin]: encode failed"
	fmt_ok=0
fi

echo
echo "=== Results ==="
echo "  round-trip:    $pass/$total pass, $fail fail"
echo "  bin-sha1:      $bin_match/$total (re-encoded matches original binary)"
if [ "$bin_mismatch" -gt 0 ]; then
	echo "  bin-mismatch:  $bin_mismatch"
fi
echo "  nr-src:        $nr_src_match/$total (src->bin->src fixpoint without -r)"
echo "  nr-bin:        $nr_bin_match/$total (src->bin->src->bin fixpoint without -r)"
if [ "$nr_src_fail" -gt 0 ]; then
	echo "  nr-src-fail:   $nr_src_fail"
fi
if [ "$nr_bin_fail" -gt 0 ]; then
	echo "  nr-bin-fail:   $nr_bin_fail"
fi
if [ "$sdb_ok" -eq 1 ]; then
	echo "  sdb-sha1:      ok (sdb.txt unchanged by ref encodes)"
else
	echo "  sdb-sha1:      FAIL (sdb.txt was modified by ref encodes)"
fi
if [ "$ref_total" -gt 0 ]; then
	echo "  ref match:     $ref_match/$ref_total"
fi
if [ "$cmt_ok" -eq 1 ]; then
	echo "  comments:      ok (// and /* */ stripped; bytecode identical)"
else
	echo "  comments:      FAIL"
fi
if [ "$fmt_ok" -eq 1 ]; then
	echo "  format:        ok (-f no-op on canonical, comments kept, idempotent)"
else
	echo "  format:        FAIL"
fi

[ "$fail" -eq 0 ] && [ "$bin_mismatch" -eq 0 ] && \
[ "$nr_src_fail" -eq 0 ] && [ "$nr_bin_fail" -eq 0 ] && [ "$sdb_ok" -eq 1 ] && \
[ "$cmt_ok" -eq 1 ] && [ "$fmt_ok" -eq 1 ]
