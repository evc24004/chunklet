# Performance

This benchmark measures a fresh 1,000 by 1,000 block area.

## Test system

| Field | Value |
| --- | --- |
| CPU | AMD Ryzen 7 5700G |
| Server threads | 8 |
| Bedrock Dedicated Server | 1.26.40.8 for Linux |
| Endstone | 0.11.7 |
| Chunklet | v0.1.2 release build with Clang 19 |
| World seed | `-809623823` |
| Dimension | Overworld |
| Center | `7000000,7000000` |

The test used this console command:

```text
chunklet start 500 overworld 7000000 7000000
```

The requested square contains 4,096 chunks. Chunklet also requests the minimum
generation border required by this BDS build: six chunks toward negative X and
five chunks on the other three edges. The native loader processes a 75 by 74
area, or 5,550 chunks. The border lets BDS finish the target chunks at the edge
of the requested square.

## Result

| Metric | Result |
| --- | ---: |
| Target chunks | 4,096 |
| Completed target chunks | 4,096 |
| Time | 16.17 seconds |
| Target throughput | 253.4 chunks per second |
| Request phase | 0.12 seconds |
| Generation phase | 15.16 seconds |
| Persistence phase | 0.88 seconds |
| Persisted target columns | 4,096 |

The run used fresh terrain. No target chunk was already loaded. Completion
included the synchronous native database commit. After shutdown, direct
inspection of the world database confirmed that all 4,096 target columns had
finalized state, Data3D, and their expected subchunks.

## Profiling result

Native generation dominated the run: 15.16 of 16.17 seconds. On a separate
matched fresh-center diagnostic, automatic server threading reached 344.2
target chunks per second and an explicit 32-thread setting reached 343.1.
Oversubscription did not improve throughput. The 500 chunks-per-second target
was not reached on this BDS build and CPU.
