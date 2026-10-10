# LevelDB compression candidate

This candidate addresses a measured generation hotspot without changing terrain
noise, compressed-file formats, configured compression levels, or LevelDB's
storage selection threshold. Its artifact is built separately from the verified
clock candidate; hosting must complete native validation before changing its pin.

## Reverse engineering evidence

The exact BDS 1.26.52.3 profiler hotspot `0x45fd2e0` implements zlib's hash-chain
longest-match search. Disassembly shows its 258-byte match limit, scalar byte
comparison loop, previous-chain index loads, and the deflate state fields used by
that algorithm. The other reported hotspot, `0xd2417d0`, is the already mapped
Perlin sample routine. It is not changed here.

The dormant global zlib optimizer was already uninstalled in the upstream
optimization revision; its retained history does not explain why. It cannot be
safely enabled on that fact alone. It replaces init/init2/deflate/end but leaves
other public state-consuming functions untouched. An exact counterexample in
this BDS is caller `0xed5128b`, which calls native `deflateBound` (`0x45fb3f0`)
after initialization. Mixing zlib-ng's private stream state with that original
function would be unsafe.

Instead this candidate hooks the complete synchronous helper at `0x40adb40`:

```cpp
bool compress(int level, const char* input, size_t length,
              std::string& output, bool raw);
```

Its 376-byte implementation owns a stack-local 112-byte z_stream, selects window
bits -15 or +15, supplies DEFLATED/memory-level-8/default-strategy, appends output,
and calls native deflateEnd before returning. The sole direct caller candidate
at `0x40ad279` is confirmed by surrounding instructions as the SST block writer:
it selects compression tag 4, passes raw=true and the configured level, then
keeps compressed data only when it saves at least 12.5%. That caller is untouched.
The retained [ELF receipt](../tools/native/leveldb-compressor-1.26.52.3.json)
records exact bytes, identity and disassembly. No version-offset guesses are used.

The hook overwrites 15 complete prologue bytes, preserving them in a trampoline.
The existing BDS build-ID gate precedes installation. Four additional exact-byte
guards cover this helper and native inflateInit2 (`0x46001d0`), inflate
(`0x4600440`), and inflateEnd (`0x4602dc0`). The latter addresses are recovered
from complete init/inflate/end sequences in BDS, and remain unmodified.

## Compatibility and fallback

Each candidate call owns its entire zlib-ng stream and appends output only after
successful compression. The public z_stream layout is statically checked at 112
bytes; the Endstone/BDS libc++ string ABI is checked at 24 bytes. No native
compression stream ever passes through zlib-ng, and no zlib-ng state reaches
native compression APIs. Stream cleanup occurs even if string allocation throws.
The native caller's compression level, raw flag, storage tag and size threshold
remain unchanged. Encoded bytes may differ while decoded bytes must match.

Before installing the hook, six raw/wrapped test streams at levels -1, 1, and 6
must decode exactly through this BDS's original inflater. The first 16 eligible
successful compression attempts additionally compress with both implementations
and decode both outputs through BDS, comparing every input byte and stream completion.
Concurrent calls beyond the 16 reserved sample slots use the original helper
until all 16 samples have passed. Unsupported inputs do not reserve slots;
failed attempts and exceptions return their reservation for a later call.
Any mismatch permanently falls back to the original helper and
returns the original output. Oversized or unsupported inputs also take the
original path. No mismatch can silently advance the validation state.

Plugin disable logs bounded aggregate calls, successful validations, mismatches,
and the first 16 samples' input bytes, output bytes and separate compression
nanoseconds. These are diagnostic CPU observations, not a whole-tile throughput
claim. Six startup checks are separate from the logged live validation count.
Native hooks now have process lifetime. The ELF requires `DF_1_NODELETE`, so
Endstone's later dlclose cannot unmap code or state still reached by background
workers. Plugin disable cancels/releases the render job and Endstone unregisters
events and tasks; it leaves native hooks and trampolines installed. A subsequent
onLoad in the same module rejects hot reload. Container restart is the upgrade
and removal mechanism. Failed initial installation retains its rollback path.

This policy also covers the existing shared-mutex optimizer: restoring its
original methods while native workers hold replacement locks would switch lock
implementations mid-operation. Its registry is deliberately process-owned and
has no ELF finalizer that destroys locks. The kernel reclaims it at process exit.
Other installed optimizer state consists of retained code, pointers, atomics,
or fixed arrays, and does not retain a ChunkletPlugin instance or callback.
An in-flight counter alone cannot prove safe unload because a thread can be
dispatched toward the hook before incrementing that counter. The pinned build
runner therefore checks the ELF residency flag, with a test accepting an actual
NODELETE shared object and rejecting an ordinary shared object. Native active-
stop proof remains required for this new artifact and is not a claim that all
possible thread interleavings were exercised.

## Standalone measurements

The reusable `tools/build-zlib-proof.sh` test performs 50 raw/wrapped cases at
levels -1, 0, 1, 6 and 9 with empty, short, repeated, high-entropy and structured
inputs, including inputs larger than its 128-KiB buffer. An independently loaded
system libz inflates each result and must reproduce exact input bytes. It also
checks append behavior and rejection without mutation for an invalid level.

A development-host run, 200 compressions per provider at level 6, measured:

| Input | Native-style system zlib bytes | zlib-ng bytes | System seconds | zlib-ng seconds | Ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| 131,073 repeated bytes | 145 | 145 | 0.0979 | 0.0491 | 1.99x |
| 262,144 random bytes | 262,224 | 262,226 | 1.1142 | 0.9486 | 1.17x |
| 262,144 structured bytes | 14,478 | 14,124 | 0.7385 | 0.6094 | 1.21x |

These are synthetic compression measurements against system zlib, not BDS or
saved-world throughput. Incompressible output can grow slightly; the unchanged
native savings threshold still decides whether to store it compressed. Required
host evidence is zero runtime validation mismatches, completed bounded generation,
clean stop, offline persisted coverage, and successful native/player reload of
the resulting save before selecting this artifact.

The earlier, superseded nonresident candidate artifact was
`.artifacts/build-zlib/endstone_chunklet.so`, SHA256
`b6462fc26e534ce0876c6b21ac4dc3b1bfe39a1f298e99af3988dcac9513d356`.
Offline audit passes 35 instruction guards (four added here), five RTTI guards,
nine vtable slots, and the initialization fingerprint. All 16 Python tooling
tests pass. Its GLIBC ceiling is 2.35. Native proof is pending at this handoff;
the existing selected clock artifact remains unchanged.

The process-resident candidate supersedes that artifact:
`.artifacts/build-zlib/endstone_chunklet.so`, SHA256
`a7d4900e07f978accf2d804da4abfec7dfe1785e0499736d24100fc48ad31483`.
Its build passes the GLIBC ceiling and actual ELF `DF_1_NODELETE` check; all 17
Python tooling tests pass, including the residency rejection test. No native
instruction guard was relaxed. Native fixture proof remains pending.

The version 0.1.7 state-validation follow-up candidate is
`.artifacts/build-state-guard/endstone_chunklet.so`, SHA256
`695391a949716d494b3aae14986f0716c80f7579a0756fce235aa9d9496444a3`.
It restores comparison of both Xoroshiro state words after noise construction
and preserves compression validation slots across unsupported inputs or failed
attempts. It also logs one synchronized shared-mutex registry entry count during
disable, without adding instrumentation to normal lock operations. This snapshot
does not establish a hard memory bound; workers can add entries afterward and
entries remain process-owned. The 64-address/eight-thread diagnostic test passes.
The exact-object ASan/UBSan test, 24-thread sampling test, 50 wire cases,
35 instruction guards, and pinned build/residency checks pass. These tests are
documented in [native validation regressions](../tools/native-validation-tests.md).
The integration owner subsequently exercised these exact bytes in image
`sha256:b35857d4fd5bb97a4024dad6423ca04378b73d8f2fad66a64154ae918c869f14`.
`wake-final-native-validation.json` records 16 successful compression checks
across 25,019 calls, zero mismatches, and an active-generation clean stop with
native and Docker exit code zero. The 16 samples total 2,642,189 input bytes:
native compression took 51,500,555 ns and produced 505,074 bytes; the candidate
took 25,285,589 ns and produced 509,237 bytes. That is approximately 2.04 times
the codec throughput with 0.824% more compressed bytes for these samples only.
The 10,000-by-10,000 saved-map inspection and final one-CPU cold boots remain
separate integration gates; no production rollout is established here.
