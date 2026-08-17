#pragma once

#include <array>
#include <cstdint>

namespace chunklet::noise::interpolation::sample_cache {
bool find(const void *self, const std::array<std::uint32_t, 3> &coordinates,
          float &value) noexcept;
void store(const void *self,
           const std::array<std::uint32_t, 3> &coordinates,
           float value) noexcept;

}  // namespace chunklet::noise::interpolation::sample_cache
