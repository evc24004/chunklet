#include "render/plan.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace chunklet::render {
namespace {

int block_to_chunk(std::int64_t block)
{
    if (block >= 0) {
        return static_cast<int>(block / 16);
    }
    return static_cast<int>(-((-block + 15) / 16));
}

__int128 distance_key(const ChunkBounds &bounds, ChunkPosition position)
{
    const auto dx =
        static_cast<__int128>(position.x) * 2 - bounds.min_x - bounds.max_x;
    const auto dz =
        static_cast<__int128>(position.z) * 2 - bounds.min_z - bounds.max_z;
    return dx * dx + dz * dz;
}

std::uint64_t spread_bits(std::uint32_t value)
{
    std::uint64_t bits = value;
    bits = (bits | bits << 16) & 0x0000ffff0000ffffULL;
    bits = (bits | bits << 8) & 0x00ff00ff00ff00ffULL;
    bits = (bits | bits << 4) & 0x0f0f0f0f0f0f0f0fULL;
    bits = (bits | bits << 2) & 0x3333333333333333ULL;
    return (bits | bits << 1) & 0x5555555555555555ULL;
}

std::uint64_t morton_key(const ChunkBounds &bounds, ChunkPosition position)
{
    const auto x = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(position.x) - bounds.min_x);
    const auto z = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(position.z) - bounds.min_z);
    return spread_bits(x) | spread_bits(z) << 1;
}

}  // namespace

std::uint64_t ChunkBounds::count() const
{
    const auto width = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(max_x) - min_x + 1);
    const auto depth = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(max_z) - min_z + 1);
    return width * depth;
}

bool ChunkBounds::contains(ChunkPosition position) const
{
    return position.x >= min_x && position.x <= max_x &&
           position.z >= min_z && position.z <= max_z;
}

ChunkBounds ChunkBounds::expanded(int chunks) const
{
    if (chunks < 0) {
        throw std::invalid_argument("chunk expansion must not be negative");
    }
    const auto expanded_min_x = static_cast<std::int64_t>(min_x) - chunks;
    const auto expanded_min_z = static_cast<std::int64_t>(min_z) - chunks;
    const auto expanded_max_x = static_cast<std::int64_t>(max_x) + chunks;
    const auto expanded_max_z = static_cast<std::int64_t>(max_z) + chunks;
    if (expanded_min_x < std::numeric_limits<int>::min() ||
        expanded_min_z < std::numeric_limits<int>::min() ||
        expanded_max_x > std::numeric_limits<int>::max() ||
        expanded_max_z > std::numeric_limits<int>::max()) {
        throw std::overflow_error("expanded chunk bounds exceed the BDS coordinate range");
    }
    return {static_cast<int>(expanded_min_x), static_cast<int>(expanded_min_z),
            static_cast<int>(expanded_max_x), static_cast<int>(expanded_max_z)};
}

ChunkBounds square_bounds(int center_x, int center_z, int radius)
{
    if (radius < 1) {
        throw std::invalid_argument("radius must be at least 1 block");
    }
    const auto min_x = static_cast<std::int64_t>(center_x) - radius;
    const auto min_z = static_cast<std::int64_t>(center_z) - radius;
    const auto max_x = static_cast<std::int64_t>(center_x) + radius - 1;
    const auto max_z = static_cast<std::int64_t>(center_z) + radius - 1;
    return {block_to_chunk(min_x), block_to_chunk(min_z),
            block_to_chunk(max_x), block_to_chunk(max_z)};
}

std::vector<ChunkPosition> row_major_order(const ChunkBounds &bounds)
{
    if (bounds.min_x > bounds.max_x || bounds.min_z > bounds.max_z) {
        throw std::invalid_argument("chunk bounds are empty");
    }
    const auto count = bounds.count();
    if (count > std::vector<ChunkPosition>().max_size()) {
        throw std::length_error("chunk target is too large");
    }

    std::vector<ChunkPosition> positions;
    positions.reserve(static_cast<std::size_t>(count));
    for (int z = bounds.min_z;; ++z) {
        for (int x = bounds.min_x;; ++x) {
            positions.push_back({x, z});
            if (x == bounds.max_x) {
                break;
            }
        }
        if (z == bounds.max_z) {
            break;
        }
    }
    return positions;
}

std::vector<ChunkPosition> morton_order(const ChunkBounds &bounds)
{
    auto positions = row_major_order(bounds);
    std::sort(positions.begin(), positions.end(), [&](const auto &left, const auto &right) {
        return morton_key(bounds, left) < morton_key(bounds, right);
    });
    return positions;
}

std::vector<ChunkPosition> center_out(const ChunkBounds &bounds)
{
    auto positions = row_major_order(bounds);
    std::sort(positions.begin(), positions.end(), [&](const auto &left, const auto &right) {
        const auto left_distance = distance_key(bounds, left);
        const auto right_distance = distance_key(bounds, right);
        if (left_distance != right_distance) {
            return left_distance < right_distance;
        }
        return left.z != right.z ? left.z < right.z : left.x < right.x;
    });
    return positions;
}

std::uint64_t chunk_key(ChunkPosition position)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.x)) << 32) |
           static_cast<std::uint32_t>(position.z);
}

}  // namespace chunklet::render
