# Native compatibility

Chunklet uses private Bedrock Dedicated Server functions. These functions do not
have a stable ABI. Use the plugin only with a supported binary.

## Supported binary

| Field | Value |
| --- | --- |
| Product | Bedrock Dedicated Server for Linux |
| Version | 1.26.52.3 |
| GNU build ID | `c0ff36e2eacfc191f01e189237df71f139b5d04a` |
| ELF SHA-256 | `0a490c711d4a2ce075debcd979e8862b7eaba008373f9970f0edbb67f98f9207` |
| Architecture | x86-64 with AVX2 |

Chunklet stops during plugin enable if the GNU build ID is different. It then
checks the RTTI names, vtable address points, vtable targets, and instruction
prefixes used by persistence. These checks run before Chunklet reads an
Endstone native handle.

## Noise shuffle optimization

For this exact build, Chunklet replaces the three 256-iteration permutation
shuffle loops inside `MultiOctaveNoiseImpl` construction with one native call
per loop. The replacement keeps Xoroshiro state in registers and implements the
same bounded-integer reduction and state transition as BDS.

Before patching, Chunklet verifies every original instruction byte at the three
sites. On the first invocation it clones the RNG state and compares all 256
bounded results plus the final state against the BDS virtual `nextInt`
implementation. An unknown RNG implementation or failed comparison uses the
original virtual calls. Plugin disable restores the original instruction bytes.

## Native evaluator optimizations

Chunklet installs AVX2 area and containment evaluators on the supported BDS
build. Each hook verifies the pinned target instructions and compares its
initial live calls bit-for-bit against the original BDS implementation. A
mismatch immediately restores the original result and permanently disables the
affected evaluator.

## Verified calls

The addresses in this table are ELF virtual addresses before PIE relocation.

| Operation | Dispatch | Target |
| --- | --- | ---: |
| `ChunkSource::getOrLoadChunk` (main/world-limit) | vtable slot 9 | `0x0ca314e0` |
| `ChunkSource::getOrLoadChunk` (network) | vtable slot 9 | `0x0c989e70` |
| `ChunkSource::flushThreadBatch` | vtable slot 29 | `0x0ca32d50` |
| `LevelStorageWriteBatch::put` | vtable slot 4 | `0x0d0d00a0` |
| `LevelChunk::markSaveIfNeverSaved` | direct | `0x0c9fc600` |
| `LevelChunk::setFinalizedState` | direct | `0x0c9da380` |
| `DBChunkStorage::_serializeChunk` | direct | `0x0d14bdc0` |

The `getOrLoadChunk` call uses this recovered Linux x86-64 ABI:

```cpp
void *getOrLoadChunk(
    std::shared_ptr<void> *result,
    void *chunk_source,
    const ChunkPosition *position,
    int load_mode,
    bool read_only);
```

Chunklet sets `load_mode` to `1` (`Deferred`) and `read_only` to `false`.
The returned object is a `std::shared_ptr<LevelChunk>`. Chunklet keeps every
reference until the target chunks emit matching Endstone `ChunkLoadEvent`
instances in native `Loaded` state (`0x0b` at offset `0xb8`).

After the final load event, Chunklet sets finalization state `2`, marks every
save category dirty, serializes each target column into the server thread's
native write batch, and inserts the `FinalizedState` (`0x36`) record. It then
calls `flushThreadBatch`, which commits that batch. Chunklet reports `Complete`
only after the commit returns.

## Resolution path

Chunklet resolves the native objects in this order:

1. Read the native `Dimension` weak-reference pointer from the Endstone
   dimension wrapper at offset `8`.
2. Call native `Dimension` vtable slot `13` to get `BlockSource`.
3. Call native `BlockSource` vtable slot `53` to get `ChunkSource`.
4. Require a known `MainChunkSource`, `NetworkChunkSource`, or
   `WorldLimitChunkSource` vtable.

## Port evidence

The 1.26.52.3 port starts from the maintained 1.26.51.1 port, commit
`13804d338dbdabe95e90e6b0508f1e5983416b03` in the enclosing Factions repository,
which imported upstream `6190b22115def1510b482d3fd1af5b8847067aec`.
The [mapping receipt](../tools/native/port-1.26.52.3.json) records both exact ELF
identities and 59 resolved addresses, including one containing-function probe.
Code candidates use unique 256-byte contexts (2,048 bytes for an initially
ambiguous mutex and the template-data probe), masking only relocation operands.
RTTI names and ELF relocation addends locate tables; vtable slot targets resolve
secondary-table ambiguity. Global pointers come from matched RIP-relative
instructions, not a blanket version offset. New data addresses shift by a
different amount from nearby code.

The offline audit verifies five RTTI names, nine vtable slots, 31 original
instruction guards, and the unchanged transient initialization fingerprint.
The Endstone 0.11.13 SDK independently retains `ChunkState::Loaded = 11` and
the dimension wrapper's native weak handle after its vptr. Native object field,
TLS write-batch, persistence key, and finalization layouts remain unchanged.
`tools/native_guard_test.cpp` executes the actual startup verifier and requires
rejection of an unknown executable before private pointer reads.

These checks qualify the candidate for an isolated runtime proof. The host
integration owner must prove plugin load, live bit-exact validators, generation,
commit, shutdown, and restart persistence against the same artifact hash.
