# Performance

This benchmark measures a fresh 1,000 by 1,000 block area.

## Test system

| Field | Value |
| --- | --- |
| CPU | AMD Ryzen 7 5700G |
| Server threads | 8 |
| Bedrock Dedicated Server | 1.26.40.8 for Linux |
| Endstone | 0.11.7 |
| Chunklet | v0.1.3 release build with Clang 19 |
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

## Optimized result

| Metric | Optimized | Unmodified baseline |
| --- | ---: | ---: |
| Target chunks | 4,096 | 4,096 |
| Completed target chunks | 4,096 | 4,096 |
| Time | 15.76 seconds | 16.93 seconds |
| Target throughput | 259.9 chunks/s | 242.0 chunks/s |
| Request phase | 0.12 seconds | 0.11 seconds |
| Generation phase | 14.72 seconds | 15.83 seconds |
| Persistence phase | 0.90 seconds | 0.98 seconds |
| Persisted target columns | 4,096 | 4,096 |

These adjacent fresh-world runs used the same binary, center, seed, and
eight-thread setting. Fusing the three permutation shuffles in BDS noise
construction improved target throughput by 7.4% and reduced elapsed time by
1.17 seconds. Separate unmodified runs ranged from 242.0 to 252.3 chunks per
second, so the result should be read as a matched-run measurement rather than a
universal gain.

Completion included the synchronous native database commit. After shutdown,
direct inspection confirmed that every target column had finalized state,
Data3D, and its expected subchunks.

## Native profile

A cycles profile of the unmodified binary attributed 9.51% of samples to
`MultiOctaveNoiseImpl` construction. The same function range received no
samples in a 42,959-sample optimized profile; the fused shuffle finished between
samples. Profiling overhead reduced absolute throughput, so throughput above
comes from unprofiled runs.

Raw databases are not byte-deterministic across fresh BDS runs: two unmodified
runs differed in the same palette and metadata record tags as the optimized
run. All other record tags matched. The optimizer additionally validates its
entire 256-step bounded-integer sequence against BDS on a cloned RNG state
before using the fused path.

## Equivalence-validated release result

The release artifact now enables only native evaluators that passed both
same-input validation and a fresh-world semantic comparison. Isolated candidate
evaluators matched BDS bit for bit across 328,559,926 Perlin calls, 90,564,332
octave calls, and 2,831,004 proximity calls. They are nevertheless disabled:
fresh worlds generated with them fell into a different semantic output family.
Matching a function return value alone was therefore not accepted as render
equivalence.

Fresh BDS worlds are not themselves deterministic. Two unmodified
one-server-thread controls with the same seed differed in 3,353 of 38,493
canonicalized subchunks. The final optimized artifact differed from an
unmodified control in 3,439 subchunks, 86 more than the control-to-control
variance. All 38,493 subchunks were present and decoded successfully.

The corresponding 16-thread release run completed all 4,096 target chunks:

| Metric | Result |
| --- | ---: |
| Time | 9.79 seconds |
| Target throughput | 418.4 chunks/s |
| Request phase | 0.10 seconds |
| Generation phase | 9.13 seconds |
| Persistence phase | 0.55 seconds |
| Completed target chunks | 4,096 |

An earlier 16-thread release build measured 501.6 chunks/s with a broader
optimizer set. The table above reports the current artifact.