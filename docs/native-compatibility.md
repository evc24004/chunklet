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
checks the RTTI names, vtable address points, and target function addresses.
These checks run before Chunklet reads an Endstone native handle.

## Verified calls

The addresses in this table are ELF virtual addresses before PIE relocation.

| Operation | Vtable slot | Main or world-limit target | Network target |
| --- | ---: | ---: | ---: |
| `ChunkSource::getOrLoadChunk` | 9 | `0x0c191870` | `0x0c1503e0` |
| `ChunkSource::saveLiveChunk` | 20 | `0x0c193650` | `0x0c193650` |
| `ChunkSource::flushThreadBatch` | 29 | `0x0c193050` | `0x0c193050` |

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
The returned object is a `std::shared_ptr<LevelChunk>`. Chunklet keeps all
references until every target chunk has a matching Endstone `ChunkLoadEvent`.

The recovered `LevelChunk::ChunkState` byte is at offset `0xb8`. Value `0x0d`
means `Loaded`. Chunklet requires this value in the load event handler before
it calls `saveLiveChunk`.

## Resolution path

Chunklet resolves the native objects in this order:

1. Read the native `Dimension` weak-reference pointer from the Endstone
   dimension wrapper at offset `8`.
2. Call native `Dimension` vtable slot `13` to get `BlockSource`.
3. Call native `BlockSource` vtable slot `53` to get `ChunkSource`.
4. Require a known `MainChunkSource`, `NetworkChunkSource`, or
   `WorldLimitChunkSource` vtable.

The implementation does not scan for signatures and does not use a fallback.
A new BDS release requires a new build ID, a new reverse-engineering review,
and a new benchmark.
