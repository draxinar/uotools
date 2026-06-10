# wombat

Encoder/decoder for Wombat compiled script bytecode, the scripting
engine embedded in the Ultima Online server.

Wombat scripts control all game logic: doors, combat, NPCs, crafting,
quests, and more. The compiled `.m` files use variant-encoded 2-byte
tokens and string database references for obfuscation.

## Build

```
make
```

## Usage

Decode a compiled bytecode file to human-readable Wombat source:

```
./wombat -d -s sdb.txt input.m output.wxx
```

Encode Wombat source text back to compiled bytecode:

```
./wombat -c -s sdb.txt input.m output.wxx
```

The `-s` flag specifies the string database file (sdb.txt), required
for both encoding and decoding. The encoder automatically adds new
strings and identifiers to the SDB and writes it back on success.

### Comments

Wombat source accepts C-style comments: `//` runs to the end of the
line, and `/* ... */` spans lines (block comments do not nest). The
encoder strips them while compiling, so commented and uncommented
source produce byte-identical bytecode. Comments have no bytecode
representation, so the decoder (`-d`) never emits them - use the
formatter (`-f`) instead to canonicalize source while keeping comments.

### Format (-f)

```
./wombat -f -s sdb.txt input.m output.m
```

Rewrites source text in the same canonical layout the decoder produces,
while preserving comments in place. The non-comment skeleton is
byte-identical to `-d` of the equivalent bytecode, so an already
canonical (comment-free) file is reproduced unchanged and the formatter
is idempotent. Integer literals are normalized to fixed-width hex, and a
block comment in the middle of an expression is moved to its own line
before the following token. `-f` is source-to-source: it never writes
bytecode and never modifies sdb.txt.

This is the canonicalization step for authoring: edit source, run
`wombat -f`, and commit the result. Unlike re-decoding the compiled
binary, it keeps your comments.

### Reference binary (-r)

The original compiler randomly selects one of 5 variant encodings per
token, making its output non-deterministic. Our encoder is
deterministic (always uses variant index 0).

To produce a byte-identical re-encoding of an existing binary, pass
the original as a reference:

```
./wombat -c -s sdb.txt -r original.m source.m output.m
```

The encoder copies variant bytes, integer encoding sizes, T_OFFSET
markers, and T_STR chain structure from the reference binary. Tokens
not present in the reference (new code) use default variants.

## Batch operations

`convert.sh` handles whole directories of scripts. Encoding always
requires a reference directory (`-r`) so the output is byte-identical
to the originals.

```
./convert.sh -d [-s sdb.txt] srcdir dstdir
./convert.sh -c [-s sdb.txt] -r refdir srcdir dstdir
./convert.sh -f [-s sdb.txt] srcdir dstdir
```

If `-s` is omitted the script looks for `sdb.txt` next to the source
directory (for `-d` and `-f`) or the reference directory (for `-c`).

## Testing

Run the round-trip test suite:

```
make test
```

Or directly:

```
./test.sh [scriptdir] [sdb.txt] [refdir]
```
