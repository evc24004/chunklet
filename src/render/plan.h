#pragma once

#include <cstdint>
#include <vector>

namespace chunklet::render {

struct ChunkPosition {
    int x;
    int z;

    bool operator==(const ChunkPosition &) const = default;
};

struct ChunkBounds {
    int min_x;
    int min_z;
    int max_x;
    int max_z;
    [[nodiscard]] ChunkBounds expanded(int chunks) const;

    [[nodiscard]] std::uint64_t count() const;
    [[nodiscard]] bool contains(ChunkPosition position) const;
};

[[nodiscard]] ChunkBounds square_bounds(int center_x, int center_z, int radius);
[[nodiscard]] std::vector<ChunkPosition> center_out(const ChunkBounds &bounds);
[[nodiscard]] std::uint64_t chunk_key(ChunkPosition position);

}  // namespace chunklet::render
