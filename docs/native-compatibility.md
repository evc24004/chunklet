# Native compatibility

Chunklet uses private Bedrock Dedicated Server functions. These functions do not
have a stable ABI. Use the plugin only with a supported binary.

## Supported binary

| Field | Value |
| --- | --- |
| Product | Bedrock Dedicated Server for Linux |
| Version | 1.26.40.8 |
| GNU build ID | `2ae1fef8c3ce6a8ebdc43f96a30c4ab307c5ff82` |
| Architecture | x86-64 |

Chunklet stops during plugin enable if the GNU build ID is different. It then
checks the RTTI names, vtable address points, vtable targets, and instruction
prefixes used by persistence. These checks run before Chunklet reads an
Endstone native handle.

## Verified calls

The addresses in this table are ELF virtual addresses before PIE relocation.

| Operation | Dispatch | Target |
| --- | --- | ---: |
| `ChunkSource::getOrLoadChunk` (main/world-limit) | vtable slot 9 | `0x0c191870` |
| `ChunkSource::getOrLoadChunk` (network) | vtable slot 9 | `0x0c1503e0` |
| `ChunkSource::flushThreadBatch` | vtable slot 29 | `0x0c193050` |
| `LevelStorageWriteBatch::put` | vtable slot 4 | `0x0c8b7580` |
| `LevelChunk::markSaveIfNeverSaved` | direct | `0x0c1b8000` |
| `LevelChunk::setFinalizedState` | direct | `0x0c1984c0` |
| `DBChunkStorage::_serializeChunk` | direct | `0x0c8e5b10` |

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
instances in native `Loaded` state (`0x0d` at offset `0xb8`).

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
