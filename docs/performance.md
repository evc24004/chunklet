# Performance

| Metric | Stock BDS | Chunklet |
| --- | ---: | ---: |
| BDS version | 1.26.40.8 | 1.26.40.8 |
| CPU | AMD Ryzen 7 5700G | AMD Ryzen 7 5700G |
| Server threads | 16 | 16 |
| World seed | `-809623823` | `-809623823` |
| Target chunks | 4,096 | 4,096 |
| Completed chunks | 4,096 | 4,096 |
| Total time | 12.98 s | 7.87 s |
| Throughput | 315.6 chunks/s | 520.6 chunks/s |
| Request phase | 0.11 s | 0.02 s |
| Generation phase | 12.08 s | 7.03 s |
| Persistence phase | 0.78 s | 0.80 s |
| Throughput improvement | — | 65.0% |

The Chunklet result is a fresh-world release run on 2026-08-15 using Endstone 0.11.7 and a 500-block square radius centered at `0,0` in the Overworld (`-32..31` on each axis). The run sustained 520.6 chunks/s across all 4,096 requested columns.

Post-shutdown validation inspected every requested database column: 4,096/4,096 were finalized, contained Data3D, and had generated subchunk height coverage. A semantic comparator decoded every palette and expanded all 38,493 persisted subchunks to per-block state sequences. Against two independent stock worlds, the optimized world had 3,382 and 3,363 differing subchunks, with zero missing records and zero parse failures; the two stock worlds differed in 3,354 subchunks. The optimized result therefore remained within fresh-world stock nondeterminism rather than introducing a new terrain family. Runtime validators also reported 8/8 bit-exact area evaluations, 16/16 direct-construction validations across 420 calls, and 4,096/4,096 monotonic-clock validations.

Perlin, octave, and proximity evaluator hooks are deliberately not installed: fresh-world semantic comparison found terrain deviations even when their sampled function-level validators passed. The 520.6 chunks/s result keeps those unsafe hooks disabled.

## Chunkize comparison

Benchmark run on 2026-08-15 against [Chunkize V1.0.4](https://github.com/ozorical/Chunkize/releases/tag/V1.0.4) (package version 1.0.3). Both plugins used fresh worlds, BDS 1.26.40.8, Endstone 0.11.7, 16 server threads, seed `-809623823`, and a 500-block square radius centered at `0,0` in the Overworld: 4,096 chunks spanning `-32..31` on each axis. Chunkize used its maximum `intense` configuration.

| Metric | Chunkize | Chunklet |
| --- | ---: | ---: |
| Reported processed chunks | 4,096 | 4,096 |
| Command completion time | 44 s | 9.33 s |
| Reported throughput | ~93.1 chunks/s | 439.1 chunks/s |
| Serviceable persisted columns | 4,046/4,096 | 4,096/4,096 |
| Throughput multiple | 1.00x | 4.72x |

Chunklet completed in 78.8% less time and delivered 371.7% more throughput. Post-shutdown database inspection required each requested column to have finalized state `2`, at least 512 bytes of Data3D, and generated subchunk height coverage. Chunklet passed all 4,096 columns. Chunkize reported all chunks processed, but 50 columns were not finalized, so it did not achieve a valid full-completion time in this run.

For completeness, [Chunkize main at `05c4790`](https://github.com/ozorical/Chunkize/commit/05c479053f2fcec2a38b3fa44d6a42d7fc1ad5ba) reported 39 seconds under the same setup, but only 713/4,096 requested columns were present after shutdown; that result is disqualified from the comparison.
