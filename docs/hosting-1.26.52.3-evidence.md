# Hosting candidate evidence, 2026-10-10

This is the source and artifact handoff for an isolated hosting fixture. It does
not claim a production rollout or completed 10,000-by-10,000-block benchmark.

| Input or output | Exact identity |
| --- | --- |
| Plugin code revision | `cc9e2a0e93b9b0b18e8b4ca8ec1c3c1dfcb40f1e` |
| Plugin SHA256 | `bc7b37def5bd54ad8b0b1a2965df9de793ef4905ad158e6bb2d7264573e1fc3c` |
| Endstone SDK | 0.11.13, `3491c609ddfde392cee2b062e3063e39aae87274` |
| BDS Linux version | 1.26.52.3 |
| BDS GNU build ID | `c0ff36e2eacfc191f01e189237df71f139b5d04a` |
| BDS SHA256 | `0a490c711d4a2ce075debcd979e8862b7eaba008373f9970f0edbb67f98f9207` |
| Offline column inspector SHA256 (superseded; see correction below) | `057d4735e573bb9ef81b29cd35fb747b02d8f4cda606e335868a335b93c3c035` |
| Hosting image used for active-stop proof | `sha256:0d317c26aa80209f32e5500815a682519d539d79f5f194156bfa9e9e104264e4` |

The selected plugin was built with the pinned Debian 12 runner into
`.artifacts/build-clock/endstone_chunklet.so`. Its highest required GLIBC symbol
version is 2.35. The retained `build/endstone_chunklet.so` is the earlier
full-halo candidate without reciprocal clock scaling; do not select it by path
alone. Hosting's `tools/hosting/chunklet-pin.json` pins the selected source and
bytes. Documentation-only commits after this code revision do not replace that
artifact pin.

The table above records the earlier clock-only candidate. The current hosting
candidate is version **0.1.7**, source
`1d201c3b1461282ae651f9bee8e56dc89a33496a`, artifact
`.artifacts/build-state-guard/endstone_chunklet.so`, SHA256
`695391a949716d494b3aae14986f0716c80f7579a0756fce235aa9d9496444a3`.
Its hosting image is
`sha256:b35857d4fd5bb97a4024dad6423ca04378b73d8f2fad66a64154ae918c869f14`.
It adds the exact guarded LevelDB helper, process-resident hooks, restored RNG
state comparison and bounded successful-validation reservations. Its audit
passes 35 instruction guards, five RTTI guards, nine vtable slots and the
initialization fingerprint, with the same GLIBC ceiling and ELF NODELETE check.

## Inspector correction after native review

The original inspector counted subchunk keys without decoding their values.
An empty value could therefore satisfy its coverage check. Its coverage result
must not be used as proof of decodable block storage.

The replacement `.artifacts/inspect-columns-strict` has SHA256
`9547c8ad876cffd43595a69bb92a672923eff4953cd9c9fadbfcf014ddd6e62a`.
It uses LevelDB source `4846fc72c7eda860b1bcf6efc58920a9273da928` plus
the maintained Snappy include patch, built with Ubuntu 24.04 GCC 13.3.0 and
`-O2`. Its runtime requires GLIBC 2.38 and GLIBCXX 3.4.32. This change does not
replace the plugin artifact or claim a new native-world receipt.

Local evidence: 46 real LevelDB fixture cases pass, including three independent
Prismarine NBT 2.8.0 fixtures. The old inspector falsely accepted 23 cases in
the initial 43-case suite. The replacement rejects 13,920 truncated prefixes
and completes 20,000 deterministic malformed-input mutations under
AddressSanitizer and UndefinedBehaviorSanitizer. LeakSanitizer could not run
under the execution sandbox's ptrace and was disabled for that instrumented
test; no leak-check result is claimed.

The stronger predicate validates all persistent v8/v9 block palettes, all
packed indices and complete bounded NBT trees. Every height-map-indicated
section must exist and decode. It fixes Nether/End height offsets, rejects
out-of-dimension sections, and preserves the machine summary format.
[Exact limits and reproduction commands](../tools/database/README.md) distinguish
structural coverage from block-registry semantics, biome/entity validation and
actual player-join proof. The integration owner must rerun stopped-database
inspection with this exact new binary before crediting coverage.

## Local verification

The final audit against the exact BDS ELF passed five RTTI guards, nine vtable
slot guards, 31 instruction guards, and the transient initialization fingerprint.
All 13 Python tooling tests passed. The actual C++ ABI guard rejected an
unsupported executable before private-pointer reads. The generation-frontier
test confirmed every radius-256 target retains six dependency columns on every
side. Clock tests passed 1,000,036 exact integer conversions and adjacent-tick
monotonicity checks, including boundaries and invalid-frequency rejection.
Commands are maintained in [the tooling guide](../tools/README.md).

## Observed native behavior

The integration owner loaded the exact guarded plugin in the isolated fixture.
Full six-column halos resolved the inherited generation stall at 992/1,024:
the first two radius-256 tiles completed at 1,024/1,024 in 12.86 and 12.14 seconds
on one CPU with one generation worker. This proves those tiles completed; full
coverage requires the separate stopped-database inspection.

The integration owner's `wake-generation-shutdown.json` receipt reports an
active-generation stop using the selected clock artifact: 0/1,089 targets
completed, 1,536 active native requests, and 489 queued when observed. The stop
request followed the active observation by 14,818 ns. Docker stop returned in
1.526 seconds; native and Docker exit codes were both zero, OOM was false, and
the final runtime status was stopped. The operation credited no generation tile.

An earlier stalled, asymmetric-halo run exited nonzero during shutdown without
a recorded native exit cause. That failure was not reproduced by this controlled
active-stop test. No speculative hook-lifetime or teardown change was made.
That historical scenario did not certify native plugin hot unload/reload or all
possible worker interleavings.

The current 0.1.7 artifact has a separate active-generation stop receipt,
`wake-final-active-stop.json`: generation 31, 170 previously completed tiles,
0/1,089 active targets completed, 1,536 active native requests and 489 queued.
Native and Docker exit codes are zero, OOM is false, and stdout EOF was drained.
`wake-final-native-validation.json` records 16 noise-state validations across
760 calls, 4,096 clock validations, and 16 successful compression validations
across 25,019 calls with zero mismatches. Sampled compression consumed
25,285,589 ns versus native 51,500,555 ns, with 509,237 versus 505,074 output
bytes. This is a codec sample, not an end-to-end generation speedup.
The shutdown registry snapshot contained zero entries for this workload.
The current implementation retains native hooks until process exit and rejects
same-module hot reload; it does not claim safe hot unloading.

Hooks already install during `onLoad`, before world initialization. Moving the
plugin's `onEnable` load order from PostWorld to Startup would not move those
patches earlier and is not included.

## Memory and cache review

The selected ELF reports 1,408,247 text bytes, 35,520 initialized data bytes, and
310,448 BSS bytes. Its TLS segment reserves 12,656 bytes per participating native
thread; this is static storage, not a measured RSS total. Major fixed arrays are
131,072 bytes for 256 transient thread slots and 158,976 bytes for containment
tables. The transient pool aborts if all 256 distinct owner slots are exhausted;
the supported hosting preparation uses one or four generation workers.

The shared-mutex optimizer keeps a 64-entry thread-local lookup cache and a
process registry of one pthread lock per distinct native mutex address. Reserving
4,096 map buckets is an initial allocation, not a hard cap. Entries remain until
process exit in the current candidate. The long generation fixture should retain memory
measurements across tiles; short-run success alone does not establish a memory
plateau. Ownership resolution reuses fields already cached by BDS and adds no
per-chunk ownership map.

A render retains every target chunk through final persistence. Sequential
radius-256 tiles bound that retention to 1,024 targets (1,936 requests with the
halo). A single radius-5000 command is unsuitable for the hosting memory budget.
Developer build trees, downloaded binaries, and tooling dependency caches remain
in ignored `.artifacts/`, `build/`, `.cache/`, or `.runtime/`; none is tracked.

## Remaining integration evidence

At this handoff the integration owner reported 32/400 full-map tiles completed.
Final 400-tile completion, authoritative persisted full-map coverage, and the
post-generation wake latency distribution remain integration-owned results.
Do not infer a complete saved map from console completion alone or equate the
clock microbenchmark with an end-to-end wake improvement. Production remains
outside this isolated source and fixture proof.
