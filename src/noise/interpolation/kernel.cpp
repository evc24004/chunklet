#include "noise/interpolation/kernel.h"

#include <array>
#include <cstdint>
#include <immintrin.h>

namespace chunklet::noise::interpolation {
namespace {
alignas(16) constexpr std::array<std::int8_t, 16> kGradientX{
    1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, 0, -1, 0};
alignas(16) constexpr std::array<std::int8_t, 16> kGradientY{
    1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, -1, 1, -1};
alignas(16) constexpr std::array<std::int8_t, 16> kGradientZ{
    0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 1, 0, -1};

float fade(float value)
{
    const float cube = value * value * value;
    return cube * ((value * 6.0F - 15.0F) * value + 10.0F);
}

float lerp(float first, float second, float amount)
{
    return (second - first) * amount + first;
}
}  // namespace

__attribute__((target("avx2")))
float sample(const void *noise, int x, int y, int z,
             float local_x, float local_y, float local_z)
{
    const auto *permutation = static_cast<const unsigned char *>(noise) + 12;
    const auto x0 = permutation[static_cast<unsigned char>(x)];
    const auto x1 = permutation[static_cast<unsigned char>(x + 1)];
    const auto xy00 = permutation[static_cast<unsigned char>(y + x0)];
    const auto xy01 = permutation[static_cast<unsigned char>(y + x0 + 1)];
    const auto xy10 = permutation[static_cast<unsigned char>(y + x1)];
    const auto xy11 = permutation[static_cast<unsigned char>(y + x1 + 1)];

    alignas(16) const unsigned char hashes[8]{
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy00)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy10)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy01)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy11)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy00 + 1)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy10 + 1)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy01 + 1)] & 15),
        static_cast<unsigned char>(permutation[static_cast<unsigned char>(z + xy11 + 1)] & 15)};
    const auto hash_values = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(hashes));
    const auto gradient_x = _mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(
        _mm_shuffle_epi8(
            _mm_load_si128(reinterpret_cast<const __m128i *>(kGradientX.data())),
            hash_values)));
    const auto gradient_y = _mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(
        _mm_shuffle_epi8(
            _mm_load_si128(reinterpret_cast<const __m128i *>(kGradientY.data())),
            hash_values)));
    const auto gradient_z = _mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(
        _mm_shuffle_epi8(
            _mm_load_si128(reinterpret_cast<const __m128i *>(kGradientZ.data())),
            hash_values)));

    const float high_x = local_x - 1.0F;
    const float high_y = local_y - 1.0F;
    const float high_z = local_z - 1.0F;
    const auto offsets_x = _mm256_setr_ps(
        local_x, high_x, local_x, high_x, local_x, high_x, local_x, high_x);
    const auto offsets_y = _mm256_setr_ps(
        local_y, local_y, high_y, high_y, local_y, local_y, high_y, high_y);
    const auto offsets_z = _mm256_setr_ps(
        local_z, local_z, local_z, local_z, high_z, high_z, high_z, high_z);
    const auto dots = _mm256_add_ps(
        _mm256_add_ps(_mm256_mul_ps(gradient_x, offsets_x),
                      _mm256_mul_ps(gradient_y, offsets_y)),
        _mm256_mul_ps(gradient_z, offsets_z));
    alignas(32) float dot[8];
    _mm256_store_ps(dot, dots);

    const float amount_x = fade(local_x);
    const float amount_y = fade(local_y);
    const float amount_z = fade(local_z);
    const float low_z = lerp(lerp(dot[0], dot[1], amount_x),
                             lerp(dot[2], dot[3], amount_x), amount_y);
    const float high_z_value = lerp(lerp(dot[4], dot[5], amount_x),
                                    lerp(dot[6], dot[7], amount_x), amount_y);
    return lerp(low_z, high_z_value, amount_z);
}

}  // namespace chunklet::noise::interpolation
