#include "noise/interpolation/sample_cache.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace chunklet::noise::interpolation::sample_cache {
namespace {
constexpr std::size_t kCacheSize = 4'096;

struct Entry {
    const void *self{};
    std::array<std::uint32_t, 3> coordinates{};
    float value{};
};

thread_local std::array<Entry, kCacheSize> cache;
thread_local Entry recent;

std::size_t index_for(
    const void *self,
    const std::array<std::uint32_t, 3> &coordinates) noexcept
{
    std::uint64_t key = reinterpret_cast<std::uintptr_t>(self) >> 3;
    key ^= (static_cast<std::uint64_t>(coordinates[0]) << 32) | coordinates[1];
    key *= 0x9e3779b97f4a7c15ULL;
    key ^= static_cast<std::uint64_t>(coordinates[2]) * 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 32;
    return static_cast<std::size_t>(key) & (kCacheSize - 1);
}
}  // namespace

bool find(const void *self, const std::array<std::uint32_t, 3> &coordinates,
          float &value) noexcept
{
    if (recent.self == self && recent.coordinates == coordinates) {
        value = recent.value;
        return true;
    }
    const auto &entry = cache[index_for(self, coordinates)];
    if (entry.self == self && entry.coordinates == coordinates) {
        value = entry.value;
        return true;
    }
    return false;
}

void store(const void *self,
           const std::array<std::uint32_t, 3> &coordinates,
           float value) noexcept
{
    recent = {self, coordinates, value};
    cache[index_for(self, coordinates)] = {self, coordinates, value};
}



}  // namespace chunklet::noise::interpolation::sample_cache
