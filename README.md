# Chunklet

Chunklet creates Bedrock chunks before players enter an area.
It is a native Endstone plugin.
It does not use ticking areas.

## Support

- Bedrock Dedicated Server 1.26.52.3 for Linux
- Endstone 0.11.13 (SDK commit `3491c609ddfde392cee2b062e3063e39aae87274`)
- x86-64 with AVX2
- Exact native ABI validation before any optimizer is installed

Chunklet checks the server binary when it starts.
It stops if the binary is not supported.

## Build

Use the isolated Debian 12 build runner with the pinned SDK, zlib-ng 2.3.3
(`12731092979c6d07f42da27da673a9f6c7b13586`), and expected-lite 0.8.0 sources.

```console
bash tools/build.sh /absolute/endstone-0.11.13 /absolute/zlib-ng /absolute/expected-lite 2
```

The artifact is `build/endstone_chunklet.so`. The runner prints its SHA-256
and rejects GLIBC requirements newer than 2.35. See [tooling](tools/README.md)
for binary audit, dependencies, build-image provenance, and native verification.

## Use

Run these commands in the server console:

```text
chunklet start <radius> <dimension> [center-x center-z]
chunklet status
chunklet stop
```

The radius is in blocks.
Valid dimensions are `overworld`, `nether`, and `the_end`.

Keep large areas in sequential bounded tiles. A run retains every target chunk
until its final database commit; a single radius-5000 run would retain roughly
392,000 columns. Radius-256 tiles retain 1,024 target columns each. A tile runner
must wait for `Complete`, record the exact requested coordinate bounds, and
resume only incomplete tiles. This preserves the existing persistence boundary.
Each tile requests a full six-chunk dependency halo on all four sides. A
radius-256 tile requests 1,936 columns including its halo and persists its
1,024 targets. `chunklet status` includes a bounded sample of up to 16 pending
target coordinates and native states, so an incomplete frontier can be
distinguished from missing completion events.

## Persistence

`Complete` means every target column has been serialized into the Bedrock
LevelDB write batch and that batch has been committed. The saved records include
the finalized state, height and biome data, and every generated subchunk.

Chunklet keeps the native chunk leases alive through the commit. A server
restart can therefore load the target columns from the database instead of
regenerating them.

The maintained mwbot harness accepts `tools/native-cycle.json` as data for its
`native-cycle` suite. It renders 16 columns at block coordinates `4096,4096`,
checks live optimizer validation, writes a gold-block marker, renders again,
gracefully stops, and restarts the same world. The marker must survive a new
render after restart. `preloaded` counts already-resident chunks, not deferred
disk loads. Inspect chunk coordinates `254 254 257 257` with the offline
database inspector after shutdown.

See [native compatibility](docs/native-compatibility.md),
[performance](docs/performance.md), and the reusable
[database column inspector](tools/database/README.md).

## License

Imported from [evc24004/chunklet](https://github.com/evc24004/chunklet) revision
`6190b22115def1510b482d3fd1af5b8847067aec`, preserving the upstream subtree
history and license. Local changes port its private native ABI and integrate
the pinned build and beta verification lanes.

Apache License 2.0.
