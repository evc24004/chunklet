# Performance

This benchmark measures a fresh 1,000 by 1,000 block area.

## Test system

| Field | Value |
| --- | --- |
| CPU | AMD Ryzen 7 5700G |
| Server threads | 8 |
| Bedrock Dedicated Server | 1.26.40.8 for Linux |
| Endstone | 0.11.7 |
| Chunklet | v0.1.1 release build with Clang 19 |
| World seed | `-809623823` |
| Dimension | Overworld |
| Center | `7000000,7000000` |

The test used this console command:

```text
chunklet start 500 overworld 7000000 7000000
```

The requested square contains 4,096 chunks. Chunklet also requested an eight-chunk
generation border. The native loader processed an 80 by 80 area, or 6,400 chunks.
The border lets BDS finish the target chunks at the edge of the requested square.

## Result

| Metric | Result |
| --- | ---: |
| Target chunks | 4,096 |
| Completed target chunks | 4,096 |
| Time | 16.69 seconds |
| Target throughput | 245.5 chunks per second |
| Persisted target columns | 4,096 |

The run used fresh terrain. No target chunk was already loaded. Completion
included the synchronous native database commit. After shutdown, direct
inspection of the world database confirmed that all 4,096 target columns had
finalized state, Data3D, and their expected subchunks.

The 500 chunks-per-second target was not reached on this system. The prior
pre-fix run reported 238.8 chunks per second but measured only target
`ChunkLoadEvent` completion and did not verify database persistence. The v0.1.1
result is not directly comparable because the terrain location changed; it
demonstrates 245.5 chunks per second with complete durable persistence.
