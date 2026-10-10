# Database column inspector

`inspect_columns.cpp` reads Bedrock's LevelDB records for a rectangular chunk
range. It verifies finalized state, a bounded height map, and structurally
decodable persistent block storage for every subchunk record. Every section
indicated by the height map must actually be present. A higher section alone
does not satisfy a missing lower section. The summary also reports logical
bytes stored in matching chunk records.

Stop the server before opening its world database.
Opening LevelDB can update its `LOG` and other bookkeeping files even though
the inspector issues no writes. Use a stopped-world gate and fingerprint the
database after inspection when creating a persistent coverage receipt.

## Build

The inspector requires a C++20 compiler, GNU Make, and the system Snappy and
zlib development packages. It uses the pinned Bedrock-compatible LevelDB fork
below. Run from the Chunklet directory; the patch fixes that revision's
nonstandard Snappy header path for the system package.

```console
git clone https://github.com/Amulet-Team/leveldb-mcpe.git
git -C leveldb-mcpe checkout 4846fc72c7eda860b1bcf6efc58920a9273da928
bash tools/build-database-inspector.sh "$PWD/leveldb-mcpe" "$PWD/.artifacts/inspect-columns" 2
```

## Use

Coordinates are chunk coordinates, not block coordinates.

```console
.artifacts/inspect-columns <world-db> <dimension> \
  <min-x> <min-z> <max-x> <max-z> [--summary]
```

Valid dimensions are `overworld`, `nether`, and `the_end`. Without
`--summary`, the command prints finalized state, Data3D size, generated
subchunks, and serviceability for each column. It exits successfully only when
every requested column is serviceable.

Here `all_serviceable=1` has a specific offline predicate:

- Every expected coordinate has an exact four-byte little-endian finalized
  state of 2 and at least 512 Data3D bytes.
- All 256 height-map entries are within the dimension height: Overworld
  `[-64,320)` (384), Nether `[0,128)` (128), End `[0,256)` (256).
  Height 0 has no occupied section; a positive value identifies the section
  containing `height + minimumY - 1`. In particular the Nether/End offset is
  `-1`, not the Overworld's `-65`.
- Every indicated section has at least one successfully decoded block-storage
  layer. Missing other sections are allowed because all-air sections may be
  omitted. All stored sections, including those above the indicated height,
  must be within the dimension and decode successfully.
- Persistent subchunk versions 8 and 9 are supported; version 9's signed
  section index must equal its LevelDB key. Each layer has a persistent
  palette flag, a supported width (`0,1,2,3,4,5,6,8,16`), the full packed word
  array, and 1–4096 palette entries (also bounded by its bit width). Every one
  of the 4096 block indices must be within the palette. Width 0 is a single
  NBT palette entry with no word array or palette count.
- Every palette entry is a complete little-endian NBT compound with a
  namespaced block `name` and a `states` compound. States must use byte,
  integer, or string values. An optional `version` must be an integer.
  All NBT types are structurally parsed, including nested lists and arrays;
  negative/truncated counts, duplicate compound fields, malformed tags,
  incomplete payloads, and trailing subchunk bytes are rejected.

Defensive bounds are 16 MiB per subchunk, 16 storage layers, NBT depth 64,
262144 NBT values per subchunk, and 262144 elements per list. Unsupported
legacy formats and data exceeding these bounds fail closed. These limits are
for current vanilla BDS saves; they are not a promise to accept every custom
world format.

This is a **structural block-storage coverage check**. It does not consult the
engine's block registry, validate the meaning of each block state/version,
compare height-map coordinates with actual non-air block positions, decode
Data3D biome storage after the height map, or validate entities, lighting and
other chunk records. It therefore does not independently prove that a player
can join or that every game-engine semantic check passes. Combine it with
native completion, clean stop, restart and player-join evidence. A physically
present empty or corrupt subchunk value never counts as generated coverage.

The format implementation follows the persistent paletted storage described
by [Mojang's block-storage format author](https://gist.github.com/Tomcc/a96af509e275b1af483b25c543cfbf37)
and the current [PocketMine LevelDB palette reader/writer](https://github.com/pmmp/PocketMine-MP/blob/stable/src/world/format/io/leveldb/LevelDB.php).
PocketMine's singleton-width encoding is significant: persistent width 0
omits the four-byte palette cardinality.

## Regression verification

```console
bash tools/test-database-inspector.sh .artifacts/leveldb-mcpe
# Optional independent fixture encoder, exactly prismarine-nbt 2.8.0:
bash tools/test-database-inspector.sh .artifacts/leveldb-mcpe \
  .artifacts/database-tests /absolute/path/to/prismarine-nbt
# Same cases with memory/undefined-behavior instrumentation:
INSPECTOR_SANITIZE=1 bash tools/test-database-inspector.sh .artifacts/leveldb-mcpe
```

The suite creates disposable real LevelDB databases and checks inspector exit
status. The original empty-payload counterexample and malformed headers,
word arrays, palettes, NBT, missing height sections and dimension-offset bugs
are negative cases. Valid cases include all supported widths, both versions,
multiple layers, negative Y, void columns and omitted all-air sections.
Independent Prismarine fixtures include nonzero packed indices, 3-bit padding
and multiple state types. The suite also rejects every truncated prefix of
five representative encodings and exercises 20000 deterministic mutations.
Fixtures are deleted on exit. In a ptrace-based sandbox LeakSanitizer cannot
operate; `ASAN_OPTIONS=detect_leaks=0` permits AddressSanitizer/UBSan checks
there, without claiming leak coverage.

Builds use `-O2` because full-map inspection decodes every block palette index.
The inspector summary's field names/order remain stable for hosting receipts;
without `--summary`, invalid payloads include the first bounded reason per
column. Diagnostics never print palette data.

Example for the 64 by 64 target region from the performance benchmark:

```console
.artifacts/inspect-columns worlds/chunklet-benchmark/db overworld \
  437468 437468 437531 437531 --summary
```

`logical_bytes` is the sum of matching LevelDB key and uncompressed value sizes.
It excludes LevelDB compression, indexes, logs, manifests, and filesystem
allocation, so it is useful for comparing regions rather than predicting the
exact world-directory size.

The reusable builder refuses a wrong LevelDB commit or unrelated source edits,
applies only the maintained system-Snappy include patch, and prints the output
SHA-256. Build on a runtime-compatible system before copying the inspector to
a host. The inspector's result is separate from an in-process `Complete` log;
the authoritative generation proof includes both plus restart persistence.
