#pragma once
#include <cstdint>

namespace chunklet::noise::interpolation {
void configure_kernel(std::uintptr_t executable_base) noexcept;


float sample(const void *noise, int x, int y, int z,
             float local_x, float local_y, float local_z);

float sample_cached(const void *noise, int x, int y, int z,
                    float local_x, float local_y, float local_z);

}  // namespace chunklet::noise::interpolation
