#pragma once
#include <cstddef>

#include <immintrin.h>

namespace chunklet::noise::fade {

inline float one(float value) noexcept
{
    const float cube = value * value * value;
    return cube * ((value * 6.0F - 15.0F) * value + 10.0F);
}

inline __m128 four(__m128 values) noexcept
{
    const auto square = _mm_mul_ps(values, values);
    const auto cube = _mm_mul_ps(square, values);
    const auto linear = _mm_sub_ps(
        _mm_mul_ps(values, _mm_set1_ps(6.0F)), _mm_set1_ps(15.0F));
    const auto polynomial = _mm_add_ps(
        _mm_mul_ps(linear, values), _mm_set1_ps(10.0F));
    return _mm_mul_ps(cube, polynomial);
}

inline __m128 three(float x, float y, float z) noexcept
{
    return four(_mm_setr_ps(x, y, z, 0.0F));
}

__attribute__((target("avx2"), always_inline))
inline __m256 eight(__m256 values) noexcept
{
    const auto square = _mm256_mul_ps(values, values);
    const auto cube = _mm256_mul_ps(square, values);
    const auto linear = _mm256_sub_ps(
        _mm256_mul_ps(values, _mm256_set1_ps(6.0F)),
        _mm256_set1_ps(15.0F));
    const auto polynomial = _mm256_add_ps(
        _mm256_mul_ps(linear, values), _mm256_set1_ps(10.0F));
    return _mm256_mul_ps(cube, polynomial);
}
__attribute__((target("avx2"), always_inline))
inline void array(float *output, const float *input, std::size_t size) noexcept
{
    std::size_t index = 0;
    for (; index + 8 <= size; index += 8) {
        _mm256_storeu_ps(output + index, eight(_mm256_loadu_ps(input + index)));
    }
    for (; index + 4 <= size; index += 4) {
        _mm_storeu_ps(output + index, four(_mm_loadu_ps(input + index)));
    }
    for (; index < size; ++index) {
        output[index] = one(input[index]);
    }
}


}  // namespace chunklet::noise::fade
