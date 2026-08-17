#pragma once

#include "spatial/containment/layout.h"

#include <array>
#include <immintrin.h>

namespace chunklet::spatial::containment {
namespace detail {
__attribute__((target("avx2"), always_inline))
inline void add_lanes(__m256d values, double &result) noexcept
{
    alignas(32) std::array<double, 4> lanes;
    _mm256_store_pd(lanes.data(), values);
    result += lanes[0];
    result += lanes[1];
    result += lanes[2];
    result += lanes[3];
}
}  // namespace detail

__attribute__((target("avx2"), always_inline))
inline __m256d type_four_batch(const Region *regions, const double *values,
                               int x, int y, int z) noexcept
{
    auto first0 = _mm_castsi128_ps(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(regions)));
    auto first1 = _mm_castsi128_ps(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(regions + 1)));
    auto first2 = _mm_castsi128_ps(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(regions + 2)));
    auto first3 = _mm_castsi128_ps(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(regions + 3)));
    _MM_TRANSPOSE4_PS(first0, first1, first2, first3);
    auto second0 = _mm_castsi128_ps(_mm_loadu_si128(
        reinterpret_cast<const __m128i *>(regions) + 1));
    auto second1 = _mm_castsi128_ps(_mm_loadu_si128(
        reinterpret_cast<const __m128i *>(regions + 1) + 1));
    auto second2 = _mm_castsi128_ps(_mm_loadu_si128(
        reinterpret_cast<const __m128i *>(regions + 2) + 1));
    auto second3 = _mm_castsi128_ps(_mm_loadu_si128(
        reinterpret_cast<const __m128i *>(regions + 3) + 1));
    _MM_TRANSPOSE4_PS(second0, second1, second2, second3);
    const auto zero = _mm_setzero_si128();
    const auto distance_x = _mm_max_epi32(
        _mm_max_epi32(
            _mm_sub_epi32(_mm_castps_si128(first0), _mm_set1_epi32(x)),
            _mm_sub_epi32(_mm_set1_epi32(x), _mm_castps_si128(first3))),
        zero);
    const auto distance_y = _mm_max_epi32(
        _mm_max_epi32(
            _mm_sub_epi32(_mm_castps_si128(first1), _mm_set1_epi32(y)),
            _mm_sub_epi32(_mm_set1_epi32(y), _mm_castps_si128(second0))),
        zero);
    const auto distance_z = _mm_max_epi32(
        _mm_max_epi32(
            _mm_sub_epi32(_mm_castps_si128(first2), _mm_set1_epi32(z)),
            _mm_sub_epi32(_mm_set1_epi32(z), _mm_castps_si128(second1))),
        zero);
    const auto maximum = _mm_max_epi32(
        _mm_max_epi32(distance_x, distance_y), distance_z);
    const auto mask = _mm_cmpgt_epi32(_mm_set1_epi32(12), maximum);
    const auto index = _mm_add_epi32(
        _mm_add_epi32(_mm_mullo_epi32(distance_x, _mm_set1_epi32(144)),
                      _mm_mullo_epi32(distance_z, _mm_set1_epi32(12))),
        distance_y);
    return _mm256_mask_i32gather_pd(
        _mm256_setzero_pd(), values, index,
        _mm256_cvtepi32_epi64(mask), 8);
}

__attribute__((target("avx2"), always_inline))
inline void add_type_four_batch(const Region *regions, const double *values,
                                int x, int y, int z,
                                double &result) noexcept
{
    detail::add_lanes(
        type_four_batch(regions, values, x, y, z), result);
}

}  // namespace chunklet::spatial::containment
