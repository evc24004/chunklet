#pragma once

#include "noise/fade/kernel.h"

#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace chunklet::noise::perlin::detail {


__attribute__((target("avx2"), always_inline))
inline __m256 gradient_dots(__m128i hashes, __m256 x, __m256 y,
                            __m256 z) noexcept
{
    const auto indices = _mm256_cvtepu8_epi32(hashes);
    const auto low_eight = _mm256_cmpgt_epi32(
        _mm256_set1_epi32(8), indices);
    const auto low_four = _mm256_cmpgt_epi32(
        _mm256_set1_epi32(4), indices);
    const auto special = _mm256_or_si256(
        _mm256_cmpeq_epi32(indices, _mm256_set1_epi32(12)),
        _mm256_cmpeq_epi32(indices, _mm256_set1_epi32(14)));
    auto u = _mm256_blendv_ps(y, x, _mm256_castsi256_ps(low_eight));
    auto v = _mm256_blendv_ps(z, y, _mm256_castsi256_ps(low_four));
    v = _mm256_blendv_ps(v, x, _mm256_castsi256_ps(special));
    const auto sign_u = _mm256_slli_epi32(
        _mm256_and_si256(indices, _mm256_set1_epi32(1)), 31);
    const auto sign_v = _mm256_slli_epi32(
        _mm256_and_si256(indices, _mm256_set1_epi32(2)), 30);
    u = _mm256_xor_ps(u, _mm256_castsi256_ps(sign_u));
    v = _mm256_xor_ps(v, _mm256_castsi256_ps(sign_v));
    return _mm256_add_ps(u, v);
}

__attribute__((target("avx2"), always_inline))
inline __m128i corner_hashes(const unsigned char *permutation,
                             int x, int y, int z) noexcept
{
    const int x0 = permutation[static_cast<unsigned char>(x)];
    const int x1 = permutation[static_cast<unsigned char>(x + 1)];
    const int xy00 = permutation[static_cast<unsigned char>(y + x0)];
    const int xy01 = permutation[static_cast<unsigned char>(y + x0 + 1)];
    const int xy10 = permutation[static_cast<unsigned char>(y + x1)];
    const int xy11 = permutation[static_cast<unsigned char>(y + x1 + 1)];
    const auto pair = [permutation](unsigned char index) noexcept {
        if (index != 255) {
            std::uint16_t value;
            std::memcpy(&value, permutation + index, sizeof(value));
            return value;
        }
        return static_cast<std::uint16_t>(
            permutation[255] | (static_cast<unsigned>(permutation[0]) << 8));
    };
    const auto pair00 = pair(static_cast<unsigned char>(z + xy00));
    const auto pair10 = pair(static_cast<unsigned char>(z + xy10));
    const auto pair01 = pair(static_cast<unsigned char>(z + xy01));
    const auto pair11 = pair(static_cast<unsigned char>(z + xy11));
    const auto pairs = _mm_setr_epi16(
        pair00, pair10, pair01, pair11, 0, 0, 0, 0);
    return _mm_shuffle_epi8(
        pairs, _mm_setr_epi8(0, 2, 4, 6, 1, 3, 5, 7,
                             -1, -1, -1, -1, -1, -1, -1, -1));
}

__attribute__((target("avx2"), always_inline))
inline float evaluate_sample(const void *noise, int x, int y, int z,
                             float local_x, float local_y,
                             float local_z) noexcept
{
    const auto *permutation = static_cast<const unsigned char *>(noise) + 12;
    const auto fades = fade::three(local_x, local_y, local_z);
    const auto fade_x =
        _mm_shuffle_ps(fades, fades, _MM_SHUFFLE(0, 0, 0, 0));
    const auto fade_y =
        _mm_shuffle_ps(fades, fades, _MM_SHUFFLE(1, 1, 1, 1));
    const auto fade_z =
        _mm_shuffle_ps(fades, fades, _MM_SHUFFLE(2, 2, 2, 2));
    const auto hashes = _mm_and_si128(
        corner_hashes(permutation, x, y, z), _mm_set1_epi8(15));
    const float high_x = local_x - 1.0F;
    const float high_y = local_y - 1.0F;
    const float high_z = local_z - 1.0F;
    const auto dots = gradient_dots(
        hashes,
        _mm256_setr_ps(local_x, high_x, local_x, high_x,
                       local_x, high_x, local_x, high_x),
        _mm256_setr_ps(local_y, local_y, high_y, high_y,
                       local_y, local_y, high_y, high_y),
        _mm256_setr_ps(local_z, local_z, local_z, local_z,
                       high_z, high_z, high_z, high_z));
    const auto even_x = _mm256_permute_ps(
        dots, _MM_SHUFFLE(2, 2, 0, 0));
    const auto odd_x = _mm256_permute_ps(
        dots, _MM_SHUFFLE(3, 3, 1, 1));
    const auto x_values = _mm256_add_ps(
        _mm256_mul_ps(
            _mm256_sub_ps(odd_x, even_x),
            _mm256_broadcastss_ps(fade_x)),
        even_x);
    const auto even_y = _mm256_permute_ps(
        x_values, _MM_SHUFFLE(0, 0, 0, 0));
    const auto odd_y = _mm256_permute_ps(
        x_values, _MM_SHUFFLE(2, 2, 2, 2));
    const auto y_values = _mm256_add_ps(
        _mm256_mul_ps(
            _mm256_sub_ps(odd_y, even_y),
            _mm256_broadcastss_ps(fade_y)),
        even_y);
    const auto low = _mm256_castps256_ps128(y_values);
    const auto high = _mm256_extractf128_ps(y_values, 1);
    return _mm_cvtss_f32(_mm_add_ss(
        _mm_mul_ss(_mm_sub_ss(high, low), fade_z), low));
}

}  // namespace chunklet::noise::perlin::detail
