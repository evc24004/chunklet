#pragma once

#include <cstddef>
#include <immintrin.h>

namespace chunklet::noise::area::tail {

__attribute__((target("avx2"), always_inline))
inline void add_two_or_three(
    float *output, std::size_t index, const float *fade_values, int iy,
    int remaining, __m128 low_delta, __m128 low_start,
    __m128 high_delta, __m128 high_start, __m128 fade_z,
    __m128 inverse) noexcept
{
    __m128 fade_y = _mm_castsi128_ps(_mm_loadl_epi64(
        reinterpret_cast<const __m128i *>(fade_values + iy)));
    __m128 previous = _mm_castsi128_ps(_mm_loadl_epi64(
        reinterpret_cast<const __m128i *>(output + index)));
    if (remaining == 3) {
        fade_y = _mm_insert_ps(
            fade_y, _mm_load_ss(fade_values + iy + 2), 0x20);
        previous = _mm_insert_ps(
            previous, _mm_load_ss(output + index + 2), 0x20);
    }
    const __m128 low = _mm_add_ps(
        _mm_mul_ps(low_delta, fade_y), low_start);
    const __m128 high = _mm_add_ps(
        _mm_mul_ps(high_delta, fade_y), high_start);
    const __m128 value = _mm_add_ps(
        _mm_mul_ps(_mm_sub_ps(high, low), fade_z), low);
    const __m128 updated = _mm_add_ps(
        previous, _mm_mul_ps(value, inverse));
    _mm_storel_epi64(
        reinterpret_cast<__m128i *>(output + index),
        _mm_castps_si128(updated));
    if (remaining == 3) {
        _mm_store_ss(
            output + index + 2,
            _mm_shuffle_ps(updated, updated, _MM_SHUFFLE(2, 2, 2, 2)));
    }
}

}  // namespace chunklet::noise::area::tail
