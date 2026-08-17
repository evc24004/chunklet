#include "spatial/containment/kernel.h"
#include "spatial/containment/batch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace chunklet::spatial::containment {
namespace {
constexpr std::size_t kAxis = 24;
constexpr std::size_t kHalfAxis = 12;
constexpr std::size_t kSmallAxis = 6;
constexpr std::size_t kDenseXStride = 0x36000;
constexpr std::size_t kDenseZStride = 0x2400;
constexpr std::size_t kDenseYStride = 0x181;


std::array<double, kAxis * kAxis * kAxis> point_values;
std::array<double, kHalfAxis * kHalfAxis * kAxis> type_two_values;
std::array<double, kSmallAxis * kSmallAxis * kAxis> type_one_values;
std::array<double, kHalfAxis * kHalfAxis * kHalfAxis> type_four_values;
const float *dense_values;

template <std::size_t Z, std::size_t Y>
constexpr std::size_t compact_index(int x, int z, int y) noexcept
{
    return (static_cast<std::size_t>(x) * Z + static_cast<std::size_t>(z)) * Y +
           static_cast<std::size_t>(y);
}

constexpr int distance_to_interval(int coordinate, int minimum,
                                   int maximum) noexcept
{
    return std::max({minimum - coordinate, coordinate - maximum, 0});
}
}  // namespace

void compact_tables(const float *dense, const float *local) noexcept
{
    dense_values = dense;
    for (int x = 0; x < 24; ++x) {
        for (int z = 0; z < 24; ++z) {
            for (int y = 0; y < 24; ++y) {
                const auto source = 0x34 + x * kDenseXStride +
                                    z * kDenseZStride + y * kDenseYStride;
                point_values[compact_index<24, 24>(x, z, y)] =
                    static_cast<double>(dense[source]) * 0.4;
            }
        }
    }
    for (int x = 0; x < 12; ++x) {
        for (int z = 0; z < 12; ++z) {
            for (int y = 0; y < 24; ++y) {
                const auto source = 0x2a3034 + x * kDenseXStride +
                                    z * kDenseZStride + y * kDenseYStride;
                type_two_values[compact_index<12, 24>(x, z, y)] =
                    static_cast<double>(dense[source]) * 0.8;
            }
        }
    }
    for (int x = 0; x < 6; ++x) {
        for (int z = 0; z < 6; ++z) {
            for (int y = 0; y < 24; ++y) {
                const auto source = 0x1c20 + x * 0x480 + z * 0x30 + y;
                type_one_values[compact_index<6, 24>(x, z, y)] =
                    static_cast<double>(local[source]);
            }
        }
    }
    for (int x = 0; x < 12; ++x) {
        for (int z = 0; z < 12; ++z) {
            for (int y = 0; y < 12; ++y) {
                const auto source = 0x1c2c + x * 0x240 + z * 0x18 + y;
                type_four_values[compact_index<12, 12>(x, z, y)] =
                    static_cast<double>(local[source]) * 0.8;
            }
        }
    }
}

__attribute__((target("avx2")))
double evaluate(const void *opaque, std::uint64_t packed_xy, int z) noexcept
{
    const auto *source = static_cast<const Source *>(opaque);
    const int x = static_cast<std::int32_t>(packed_xy);
    const int y = static_cast<std::int32_t>(packed_xy >> 32);
    double result = 0.0;
    const Region *region = source->regions_begin;
    while (region != source->regions_end) {
        if (source->regions_end - region >= 8 &&
            region[0].type == 4 && region[1].type == 4 &&
            region[2].type == 4 && region[3].type == 4 &&
            region[4].type == 4 && region[5].type == 4 &&
            region[6].type == 4 && region[7].type == 4) {
            const auto first = type_four_batch(
                region, type_four_values.data(), x, y, z);
            const auto second = type_four_batch(
                region + 4, type_four_values.data(), x, y, z);
            detail::add_lanes(first, result);
            detail::add_lanes(second, result);
            region += 8;
            continue;
        }
        if (source->regions_end - region >= 4 &&
            region[0].type == 4 && region[1].type == 4 &&
            region[2].type == 4 && region[3].type == 4) {
            add_type_four_batch(
                region, type_four_values.data(), x, y, z, result);
            region += 4;
            continue;
        }
        const Region &current = *region++;
        const int dx = distance_to_interval(x, current.min_x, current.max_x);
        const int dz = distance_to_interval(z, current.min_z, current.max_z);
        double value = 0.0;
        if (current.type == 4) {
            const int dy =
                distance_to_interval(y, current.min_y, current.max_y);
            if (std::max({dx, dy, dz}) < 12) {
                value = type_four_values[compact_index<12, 12>(dx, dz, dy)];
            }
        } else {
            const int shifted_min_y = current.min_y + current.shift_y;
            const int raw_y = y - shifted_min_y;
            if (current.type == 1) {
                const unsigned dy = static_cast<unsigned>(raw_y + 12);
                if (dy < 24 && std::max(dx, dz) < 6) {
                    value = type_one_values[compact_index<6, 24>(dx, dz, dy)];
                }
            } else if (current.type == 2) {
                const unsigned dy = static_cast<unsigned>(raw_y + 12);
                if (dy < 24 && std::max(dx, dz) < 12) {
                    value = type_two_values[compact_index<12, 24>(dx, dz, dy)];
                }
            } else if (current.type == 3) {
                const int dy =
                    distance_to_interval(y, shifted_min_y, current.max_y);
                if (std::max({dx, dy, dz}) < 12) {
                    const auto index = 0x2a4240 + dx * kDenseXStride +
                                       dz * kDenseZStride + dy * 0x180 + raw_y;
                    value = static_cast<double>(dense_values[index]) * 0.8;
                }
            }
        }
        result += value;
    }
    double point_result = 0.0;
    for (const Point *point = source->points_begin;
         point != source->points_end; ++point) {
        const unsigned dx = static_cast<unsigned>(x - point->x + 12);
        const unsigned dy = static_cast<unsigned>(y - point->y + 12);
        const unsigned dz = static_cast<unsigned>(z - point->z + 12);
        double value = 0.0;
        if (std::max({dx, dy, dz}) < 24) {
            value = point_values[compact_index<24, 24>(dx, dz, dy)];
        }
        point_result += value;
    }
    return result + point_result;
}

}  // namespace chunklet::spatial::containment
