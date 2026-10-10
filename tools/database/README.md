# Database column inspector

`inspect_columns.cpp` reads Bedrock's LevelDB records for a rectangular chunk
range. It verifies that every column has finalized state, Data3D, and generated
subchunks covering its height map. The summary also reports the logical bytes
stored in matching chunk records.

Stop the server before opening its world database.

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
