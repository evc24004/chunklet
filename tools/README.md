# Native compatibility tooling

`native_abi.py` compares exact old/new Linux x86-64 BDS ELF inputs without
executing either. It reports SHA-256 and GNU build IDs, unique instruction
matches with only relative call/jump and RIP-relative displacement bytes
masked, and RTTI-derived address candidates. Structure field displacements
remain exact. Candidate matches are investigation evidence, not permission to
admit a runtime: verify full functions, referenced data, slot targets, native
object layout and all overwritten bytes before changing a build-ID guard.

```
python3 -m pip install --target .artifacts/python capstone==5.0.6 pyelftools==0.32
PYTHONPATH=.artifacts/python python3 -m unittest discover -s tools -p native_abi_test.py
PYTHONPATH=.artifacts/python python3 tools/native_abi.py OLD_ELF NEW_ELF --source src --output .artifacts/native/report.json
```

Dependencies and binaries stay in ignored `.artifacts/`. The tool does not
edit source, execute native instructions, or select ambiguous candidates.

Add `--reuse REPORT.json` to reuse uniquely resolved candidates after checking
both input hashes. `--probe 0xADDRESS` adds an explicitly inspected containing
function, useful for data references before a patch site. This port uses
`--probe 0x51886f0` for transient terrain template loads. The tool narrows RTTI
tables by offset-to-top and mapped slot targets; unresolved candidates remain
visible. [The retained receipt](native/port-1.26.52.3.json) records this port.

`port_native.py REPORT.json --source src` previews a resolved report; `--apply`
updates address literals and original-byte guards after verifying exact input
identities. Apply only to the receipt's old source version, then review the
diff. It does not infer object field layouts or change persistence behavior.

Audit the resulting source and actual production rejection guard:

```
PYTHONPATH=.artifacts/python python3 tools/verify_native.py BDS_ELF --source src
g++ -std=c++20 -Isrc tools/native_guard_test.cpp src/native/layout.cpp -o .artifacts/native_guard_test
.artifacts/native_guard_test
PYTHONPATH=.artifacts/python python3 -m unittest discover -s tools -p '*_test.py'
```

`verify_native.py` requires the exact supported build ID before reading native
locations, then checks every runtime RTTI, slot, patch-byte and initialization
fingerprint guard, including currently dormant evaluator/compression hooks.

`build.sh SDK_SOURCE ZLIB_SOURCE EXPECTED_LITE_SOURCE [JOBS]` builds only this
plugin, with network access disabled and dependency mounts read-only. It uses
the existing maintained Debian 12 / Clang 19 image
`sha256:e35341094aed475b05143b1424e1ae3b449c845b001ca000c700e749a8f35576`
(enclosing project's `endstone/Dockerfile.debian12-toolchain`, source SHA-256
`1a0d5b4e9acde51f1159074df81c080d06b424225b71907b93a0d3b219a9fa93`).
The SDK must be clean at `3491c609ddfde392cee2b062e3063e39aae87274`.
`verify_inputs.py` checks zlib-ng and expected-lite content, paths, symlinks and
executable bits against the inherited SDK pins before starting the container.
The zlib static target explicitly enables PIC so standalone builds have the
same loadable shared-library behavior as the enclosing build.

Without that local image, build with a compatible Debian 12 Clang/libc++
toolchain and standard CMake inputs:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DCHUNKLET_ENDSTONE_SOURCE=/absolute/endstone-0.11.13 \
  -DFETCHCONTENT_SOURCE_DIR_ZLIB_NG=/absolute/zlib-ng \
  -DFETCHCONTENT_SOURCE_DIR_EXPECTED-LITE=/absolute/expected-lite
cmake --build build --target chunklet --parallel 2
```

An actual serving runtime must pass `tools/native-cycle.json` (fresh generation,
live optimizer validators, commit, shutdown, restart, persisted block marker).
Large benchmark areas must use bounded sequential tiles; see the main README.

Generation-frontier regression:

```
g++ -std=c++20 -Isrc tools/render_plan_test.cpp src/render/plan.cpp -o .artifacts/render_plan_test
.artifacts/render_plan_test
```

The previous asymmetric plan omitted one dependency column on three sides;
the hosting radius-256 tile stalled at 992/1,024 with that plan. This test preserves
all six neighbors for every target in negative, zero and positive coordinate
tiles. Actual completion still requires native Loaded events and a successful
database commit; the halo test alone does not certify runtime completion.
