#!/usr/bin/env bash
# Usage: tools/build-zlib-proof.sh [EXISTING_PINNED_BUILD_DIR]
# Compile/run isolated sampling, wire compatibility and CPU/storage benchmark using that
# build's pinned static zlib-ng and independent Debian12 libz.so.1 inflater.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=$(realpath "${1:-$root/.artifacts/build-clock}")
[[ "$build_dir" == "$root/"* ]] || { echo 'Build must stay in this checkout' >&2; exit 2; }
relative=${build_dir#"$root/"}
docker run --rm --network none -v "$root:/work" -w /work sha256:e35341094aed475b05143b1424e1ae3b449c845b001ca000c700e749a8f35576 bash -euc '
clang++-19 -O2 -std=c++20 -stdlib=libc++ -ffunction-sections -fdata-sections -Isrc -I"$1/_deps/zlib_ng-build" tools/leveldb_sampling_test.cpp src/compression/zlib/leveldb_kernel.cpp "$1/_deps/zlib_ng-build/libz.a" -Wl,--gc-sections -ldl -pthread -o .artifacts/leveldb_sampling_test
.artifacts/leveldb_sampling_test
clang++-19 -O3 -std=c++20 -stdlib=libc++ -Isrc -I"$1/_deps/zlib_ng-build" tools/zlib_wire_test.cpp src/compression/zlib/leveldb_kernel.cpp "$1/_deps/zlib_ng-build/libz.a" -ldl -o .artifacts/zlib_wire_test
.artifacts/zlib_wire_test
' bash "$relative"
