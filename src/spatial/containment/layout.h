#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace chunklet::spatial::containment {

struct Region {
    std::int32_t min_x;
    std::int32_t min_y;
    std::int32_t min_z;
    std::int32_t max_x;
    std::int32_t max_y;
    std::int32_t max_z;
    std::uint8_t type;
    std::array<std::byte, 3> padding;
    std::int32_t shift_y;
};
static_assert(sizeof(Region) == 32);

struct Point {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::array<std::byte, 8> payload;
};
static_assert(sizeof(Point) == 20);

struct Source {
    const Region *regions_begin;
    const Region *regions_end;
    const Region *regions_capacity;
    const Point *points_begin;
    const Point *points_end;
};

}  // namespace chunklet::spatial::containment
