#!/usr/bin/env bash
# Build the standalone hosting plugin using the maintained Debian12/Clang19
# image. Usage: tools/build.sh SDK_SOURCE ZLIB_SOURCE EXPECTED_LITE_SOURCE [JOBS] [BUILD_DIR]
# Input paths must be absolute. BUILD_DIR defaults to build and must stay inside
# this checkout. Output: BUILD_DIR/endstone_chunklet.so and SHA256.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
sdk=$(realpath "${1:?SDK source required}")
zlib=$(realpath "${2:?zlib source required}")
expected=$(realpath "${3:?expected-lite source required}")
jobs=${4:-2}
build_dir=$(realpath -m "$root/${5:-build}")
[[ "$build_dir" == "$root/"* ]] || { echo 'BUILD_DIR must stay inside this checkout' >&2; exit 2; }
container_build="/work/${build_dir#"$root/"}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 2; }
[[ $(git -C "$sdk" rev-parse HEAD) == 3491c609ddfde392cee2b062e3063e39aae87274 ]] || { echo 'SDK must be Endstone 0.11.13 pinned commit' >&2; exit 2; }
[[ -z $(git -C "$sdk" status --porcelain --untracked-files=no) ]] || { echo 'SDK source is modified' >&2; exit 2; }
python3 "$root/tools/verify_inputs.py" "$zlib" "$expected"
image=sha256:e35341094aed475b05143b1424e1ae3b449c845b001ca000c700e749a8f35576
docker run --rm --network none --user 0 --platform linux/amd64 \
    -v "$root:/work" -v "$sdk:/sdk:ro" -v "$zlib:/zlib:ro" -v "$expected:/expected:ro" \
    -w /work "$image" bash -euc '
        cmake -S . -B "$2" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=/usr/bin/clang-19 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-19 \
            -DCHUNKLET_ENDSTONE_SOURCE=/sdk -DFETCHCONTENT_SOURCE_DIR_ZLIB_NG=/zlib \
            -DFETCHCONTENT_SOURCE_DIR_EXPECTED-LITE=/expected \
            -DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++
        cmake --build "$2" --target chunklet --parallel "$1"
        maximum=$(objdump -T "$2/endstone_chunklet.so" | grep -o "GLIBC_[0-9.]*" | sort -Vu | tail -n1)
        case "$maximum" in GLIBC_2.3[0-5]|GLIBC_2.[0-2][0-9]|GLIBC_2.[0-9]) ;; *) echo "incompatible GLIBC: $maximum" >&2; exit 1;; esac
        if nm -D "$2/endstone_chunklet.so" | grep -Eq "__isoc23_|__isoc99_.*_chk"; then echo "unexpected C23 symbols" >&2; exit 1; fi
        python3 tools/verify_residency.py "$2/endstone_chunklet.so"
        sha256sum "$2/endstone_chunklet.so"
    ' bash "$jobs" "$container_build"
