#!/usr/bin/env bash
# Usage: bash tools/test-database-inspector.sh LEVELDB_SOURCE [OUTPUT_DIRECTORY] [PRISMARINE_NBT_MODULE]
# Builds real LevelDB positive/negative fixtures in a disposable directory.
# Requires the exact source/static library prepared by build-database-inspector.sh.
# Set INSPECTOR_SANITIZE=1 for AddressSanitizer + UndefinedBehaviorSanitizer.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
source_root=$(realpath "${1:?LEVELDB_SOURCE required}")
output=$(realpath -m "${2:-$root/.artifacts/database-tests}")
[[ $(git -C "$source_root" rev-parse HEAD) == 4846fc72c7eda860b1bcf6efc58920a9273da928 ]] || exit 2
mkdir -p "$output"
flags=(-O2)
if [[ ${INSPECTOR_SANITIZE:-0} == 1 ]]; then
    flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
c++ "${flags[@]}" -std=c++20 -DDLLX= -I"$source_root/include" \
    "$root/tools/database/inspect_columns_test.cpp" "$source_root/out-static/libleveldb.a" \
    -lsnappy -lz -pthread -o "$output/inspect-columns-test"
fixtures=$(mktemp -d "$output/fixtures.XXXXXX")
trap 'rm -rf -- "$fixtures"' EXIT
if [[ -n ${3:-} ]]; then
    node "$root/tools/database/generate_nbt_fixtures.cjs" "$3" "$fixtures/oracle"
    "$output/inspect-columns-test" "$fixtures" "$fixtures/oracle"
else
    "$output/inspect-columns-test" "$fixtures"
fi
