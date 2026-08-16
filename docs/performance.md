# Performance

This benchmark measures a fresh 1,000 by 1,000 block area.

## Test system

| Field | Value |
| --- | --- |
| CPU | AMD Ryzen 7 5700G |
| Server threads | 8 |
| Bedrock Dedicated Server | 1.26.40.8 for Linux |
| Endstone | 0.11.7 |
| Chunklet | Release build with Clang 19 |
| World seed | `-809623823` |
| Dimension | Overworld |
| Center | `6000000,6000000` |

The test used this console command:

```text
chunklet start 500 overworld 6000000 6000000
```

The requested square contains 4,096 chunks. Chunklet also requested an eight-chunk
generation border. The native loader processed an 80 by 80 area, or 6,400 chunks.
The border lets BDS finish the target chunks at the edge of the requested square.

## Result

| Metric | Result |
| --- | ---: |
| Target chunks | 4,096 |
| Completed target chunks | 4,096 |
| Time | 17.16 seconds |
| Target throughput | 238.8 chunks per second |

The run used fresh terrain. No target chunk was already loaded. The result came
from the plugin completion event, after the final native save batch completed.

The 500 chunks-per-second target was not reached on this system. A change that
saved only target chunks and flushed once at completion gave 238.8 chunks per
second. The prior result was 238.2 chunks per second. Native BDS generation was
the limiting work in this test.
