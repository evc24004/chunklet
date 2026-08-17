#include "noise/interpolation/kernel.h"

namespace chunklet::noise::interpolation {
namespace {
constexpr std::uintptr_t kNativeSample = 0xc9ca690;

using NativeSample = float (*)(const void *, int, int, int, float, float, float);
NativeSample native_sample;
}  // namespace

void configure_kernel(std::uintptr_t executable_base) noexcept
{
    native_sample =
        reinterpret_cast<NativeSample>(executable_base + kNativeSample);
}

float sample(const void *noise, int x, int y, int z,
             float local_x, float local_y, float local_z)
{
    return native_sample(noise, x, y, z, local_x, local_y, local_z);
}

float sample_cached(const void *noise, int x, int y, int z,
                    float local_x, float local_y, float local_z)
{
    return sample(noise, x, y, z, local_x, local_y, local_z);
}

}  // namespace chunklet::noise::interpolation
