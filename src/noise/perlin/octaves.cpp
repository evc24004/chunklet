#include "noise/perlin/octaves.h"

#include "noise/perlin/evaluator.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace chunklet::noise::perlin {
namespace {
constexpr std::size_t kOctaveSize = 0x124;
constexpr float kSecondaryScale = std::bit_cast<float>(0x3f8251fbU);

template <typename Value>
Value load(const unsigned char *address)
{
    Value value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

__attribute__((target("avx2"), always_inline))
float sample(const unsigned char *octave, float x, float y, float z)
{
    const float frequency = load<float>(octave + 0x114);
    const __m128 coordinates = _mm_setr_ps(x, y, z, 0.0F);
    const __m128 offsets = _mm_setr_ps(
        load<float>(octave), load<float>(octave + 4),
        load<float>(octave + 8), 0.0F);
    const __m128 adjusted = _mm_add_ps(
        _mm_mul_ps(coordinates, _mm_set1_ps(frequency)), offsets);
    const __m128 floors = _mm_floor_ps(adjusted);
    const __m128i lattice = _mm_cvttps_epi32(floors);
    const __m128 local = _mm_sub_ps(adjusted, floors);
    return detail::evaluate_sample(
        octave, _mm_extract_epi32(lattice, 0),
        _mm_extract_epi32(lattice, 1), _mm_extract_epi32(lattice, 2),
        _mm_cvtss_f32(local),
        _mm_cvtss_f32(_mm_shuffle_ps(local, local, 0x55)),
        _mm_cvtss_f32(_mm_shuffle_ps(local, local, 0xaa)));
}

__attribute__((target("avx2"), always_inline))
float sum(const unsigned char *begin, const unsigned char *end,
          float x, float y, float z)
{
    float result = 0.0F;
    for (auto *octave = begin; octave != end; octave += kOctaveSize) {
        if (octave[0x120] == 1) {
            result += sample(octave, x, y, z) * load<float>(octave + 0x11c);
        }
    }
    return result;
}

__attribute__((target("avx2"), always_inline))
bool sum_pair(const unsigned char *primary, const unsigned char *primary_end,
              const unsigned char *secondary, const unsigned char *secondary_end,
              float x, float y, float z,
              float &primary_value, float &secondary_value)
{
    if (primary_end - primary != secondary_end - secondary) {
        return false;
    }
    primary_value = 0.0F;
    secondary_value = 0.0F;
    const float secondary_x = x * kSecondaryScale;
    const float secondary_y = y * kSecondaryScale;
    const float secondary_z = z * kSecondaryScale;
    for (; primary != primary_end;
         primary += kOctaveSize, secondary += kOctaveSize) {
        if (primary[0x120] != secondary[0x120]) {
            return false;
        }
        if (primary[0x120] == 1) {
            primary_value +=
                sample(primary, x, y, z) * load<float>(primary + 0x11c);
            secondary_value += sample(
                secondary, secondary_x, secondary_y, secondary_z) *
                load<float>(secondary + 0x11c);
        }
    }
    return true;
}
}  // namespace

__attribute__((target("avx2")))
float evaluate_octaves(const void *self, float x, float y, float z)
{
    const auto *object = static_cast<const unsigned char *>(self);
    const auto *primary = load<const unsigned char *>(object);
    const auto *primary_end = load<const unsigned char *>(object + 8);
    const auto *secondary = load<const unsigned char *>(object + 0x18);
    const auto *secondary_end = load<const unsigned char *>(object + 0x20);
    float primary_value;
    float secondary_value;
    if (!sum_pair(primary, primary_end, secondary, secondary_end,
                  x, y, z, primary_value, secondary_value)) {
        primary_value = sum(primary, primary_end, x, y, z);
        secondary_value = sum(
            secondary, secondary_end, x * kSecondaryScale,
            y * kSecondaryScale, z * kSecondaryScale);
    }
    return (primary_value + secondary_value) * load<float>(object + 0x30);
}

}  // namespace chunklet::noise::perlin
