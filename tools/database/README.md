# Database column inspector

`inspect_columns.cpp` reads Bedrock's LevelDB records for a rectangular chunk
range. It verifies that every column has finalized state, Data3D, and generated
subchunks covering its height map. The summary also reports the logical bytes
stored in matching chunk records.

Stop the server before opening its world database.

## Build

The inspector uses the Bedrock-compatible LevelDB fork:

```console
git clone https://github.com/Amulet-Team/leveldb-mcpe.git
git -C leveldb-mcpe checkout 4846fc72c7eda860b1bcf6efc58920a9273da928
make -C leveldb-mcpe -j
c++ -std=c++20 -DDLLX= \
  -Ileveldb-mcpe/include \
  tools/database/inspect_columns.cpp \
  leveldb-mcpe/out-static/libleveldb.a \
  -lsnappy -lz -pthread -o inspect-columns
```

## Use

Coordinates are chunk coordinates, not block coordinates.

```console
./inspect-columns <world-db> <dimension> \
  <min-x> <min-z> <max-x> <max-z> [--summary]
```

Valid dimensions are `overworld`, `nether`, and `the_end`. Without
`--summary`, the command prints finalized state, Data3D size, generated
subchunks, and serviceability for each column. It exits successfully only when
every requested column is serviceable.

Example for the 64 by 64 target region from the performance benchmark:

```console
./inspect-columns worlds/chunklet-benchmark/db overworld \
  437468 437468 437531 437531 --summary
```

`logical_bytes` is the sum of matching LevelDB key and uncompressed value sizes.
It excludes LevelDB compression, indexes, logs, manifests, and filesystem
allocation, so it is useful for comparing regions rather than predicting the
exact world-directory size.
