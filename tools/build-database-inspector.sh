#!/usr/bin/env bash
# Build the offline Bedrock column inspector from the exact LevelDB source.
# Usage: bash tools/build-database-inspector.sh LEVELDB_SOURCE OUTPUT [JOBS]
# Requires C++20, make, Snappy/zlib development headers. Fetch the source with:
# git clone https://github.com/Amulet-Team/leveldb-mcpe.git .artifacts/leveldb-mcpe
# git -C .artifacts/leveldb-mcpe checkout --detach 4846fc72c7eda860b1bcf6efc58920a9273da928
# Source must be a dedicated checkout; only the documented Snappy include patch
# is allowed. Run the resulting inspector only on a stopped database or copy.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
source_root=$(realpath "${1:?LEVELDB_SOURCE required}")
output=$(realpath -m "${2:?OUTPUT required}")
jobs=${3:-2}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 2; }
expected=4846fc72c7eda860b1bcf6efc58920a9273da928
[[ $(git -C "$source_root" rev-parse HEAD) == "$expected" ]] || { echo 'LevelDB source pin mismatch' >&2; exit 2; }
changed=$(git -C "$source_root" diff --name-only HEAD)
[[ -z "$changed" || "$changed" == db/snappy_compressor.cc ]] || { echo 'Unrelated LevelDB source edits refused' >&2; exit 2; }
patch="$root/tools/database/leveldb-system-snappy.patch"
if git -C "$source_root" apply --check "$patch" 2>/dev/null; then
    git -C "$source_root" apply "$patch"
elif ! git -C "$source_root" apply --reverse --check "$patch" 2>/dev/null; then
    echo 'LevelDB Snappy source does not match the maintained patch' >&2
    exit 2
fi
make -C "$source_root" -j"$jobs" out-static/libleveldb.a
mkdir -p "$(dirname "$output")"
c++ -O2 -std=c++20 -DDLLX= -I"$source_root/include" \
    "$root/tools/database/inspect_columns.cpp" "$source_root/out-static/libleveldb.a" \
    -lsnappy -lz -pthread -o "$output"
sha256sum "$output"
