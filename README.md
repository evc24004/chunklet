# Chunklet

Chunklet creates Bedrock chunks before players enter an area.
It is a native Endstone plugin.
It does not use ticking areas.

## Support

- Bedrock Dedicated Server 1.26.40.8 for Linux
- Endstone 0.11.7
- x86-64

Chunklet checks the server binary when it starts.
It stops if the binary is not supported.

## Build

Requirements: CMake 3.20, Clang 19, and a C++20 toolchain with libc++.

```console
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHUNKLET_ENDSTONE_SOURCE=/path/to/endstone
cmake --build build
```

Copy `build/endstone_chunklet.so` to the Endstone `plugins` directory.
Restart the server.

## Use

Run these commands in the server console:

```text
chunklet start <radius> <dimension> [center-x center-z]
chunklet status
chunklet stop
```

The radius is in blocks.
Valid dimensions are `overworld`, `nether`, and `the_end`.

See [native compatibility](docs/native-compatibility.md) and
[performance](docs/performance.md).

## License

Apache License 2.0.
