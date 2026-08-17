#include "noise/interpolation/octaves.h"

#include "noise/interpolation/kernel.h"

#include <cstddef>
#include <cstring>

namespace chunklet::noise::interpolation::octaves {
namespace {
constexpr int kNoiseSize = 0x124;
constexpr int kEnabledOffset = 0x120;

float read_float(const unsigned char *address)
{
    float value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

float evaluate_impl(const unsigned char *begin, const unsigned char *end,
                    float x, float y, float z)
{
    float result = 0.0F;
    for (; begin != end; begin += kNoiseSize) {
        if (begin[kEnabledOffset] != 1) {
            continue;
        }
        const float frequency = read_float(begin + 0x114);
        const float sample_x = x * frequency + read_float(begin);
        const float sample_y = y * frequency + read_float(begin + 4);
        const float sample_z = z * frequency + read_float(begin + 8);
        const int floor_x = static_cast<int>(__builtin_floorf(sample_x));
        const int floor_y = static_cast<int>(__builtin_floorf(sample_y));
        const int floor_z = static_cast<int>(__builtin_floorf(sample_z));
        result += sample(begin, floor_x, floor_y, floor_z,
                         sample_x - static_cast<float>(floor_x),
                         sample_y - static_cast<float>(floor_y),
                         sample_z - static_cast<float>(floor_z)) *
                  read_float(begin + 0x11c);
    }
    return result;
}
}  // namespace

float evaluate(const unsigned char *begin, const unsigned char *end,
               float x, float y, float z)
{
    return evaluate_impl(begin, end, x, y, z);
}

float evaluate_cached(const unsigned char *begin, const unsigned char *end,
                      float x, float y, float z)
{
    return evaluate_impl(begin, end, x, y, z);
}

}  // namespace chunklet::noise::interpolation::octaves
