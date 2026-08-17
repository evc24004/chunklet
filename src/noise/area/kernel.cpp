#include "noise/area/kernel.h"
#include "noise/fade/kernel.h"
#include "noise/area/tail.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
namespace chunklet::noise::area {
namespace {
float read_float(const unsigned char *address)
{
    float value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
__attribute__((target("avx2"), always_inline))
__m256 gradient_dots(__m128i hashes, __m256 x, __m256 y, __m256 z)
{
    const auto indices = _mm256_cvtepu8_epi32(hashes);
    const auto low_eight = _mm256_cmpgt_epi32(
        _mm256_set1_epi32(8), indices);
    const auto low_four = _mm256_cmpgt_epi32(
        _mm256_set1_epi32(4), indices);
    const auto special = _mm256_or_si256(
        _mm256_cmpeq_epi32(indices, _mm256_set1_epi32(12)),
        _mm256_cmpeq_epi32(indices, _mm256_set1_epi32(14)));
    auto u = _mm256_blendv_ps(
        y, x, _mm256_castsi256_ps(low_eight));
    auto v = _mm256_blendv_ps(
        z, y, _mm256_castsi256_ps(low_four));
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
__m128i corner_hashes(const int *permutation, int x0, int x1,
                      int y, int z)
{
    const int xy00 = permutation[static_cast<unsigned char>(y + x0)];
    const int xy01 = permutation[static_cast<unsigned char>(y + x0 + 1)];
    const int xy10 = permutation[static_cast<unsigned char>(y + x1)];
    const int xy11 = permutation[static_cast<unsigned char>(y + x1 + 1)];
    const auto pair = [permutation, z](int xy) {
        std::uint64_t packed;
        std::memcpy(
            &packed,
            permutation + static_cast<unsigned char>(z + xy),
            sizeof(packed));
        return packed;
    };
    const auto first = _mm_set_epi64x(pair(xy10), pair(xy00));
    const auto second = _mm_set_epi64x(pair(xy11), pair(xy01));
    const auto order = _mm_setr_epi8(
        0, 8, 4, 12, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1);
    return _mm_unpacklo_epi16(
        _mm_shuffle_epi8(first, order),
        _mm_shuffle_epi8(second, order));
}
__attribute__((target("avx2")))
__m128 dots(const int *permutation, int x0, int x1, int y, int z,
            float local_x, float local_y, float local_z, float fade_x)
{
    auto hashes = _mm_and_si128(
        corner_hashes(permutation, x0, x1, y, z), _mm_set1_epi8(15));
    const float high_x = local_x - 1.0F;
    const float high_y = local_y - 1.0F;
    const float high_z = local_z - 1.0F;
    const auto offsets_x = _mm256_setr_ps(
        local_x, high_x, local_x, high_x, local_x, high_x, local_x, high_x);
    const auto offsets_y = _mm256_setr_ps(
        local_y, local_y, high_y, high_y, local_y, local_y, high_y, high_y);
    const auto offsets_z = _mm256_setr_ps(
        local_z, local_z, local_z, local_z, high_z, high_z, high_z, high_z);
    const auto values =
        gradient_dots(hashes, offsets_x, offsets_y, offsets_z);
    const auto even = _mm256_permutevar8x32_ps(
        values, _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0));
    const auto odd = _mm256_permutevar8x32_ps(
        values, _mm256_setr_epi32(1, 3, 5, 7, 0, 0, 0, 0));
    const auto low_even = _mm256_castps256_ps128(even);
    const auto low_odd = _mm256_castps256_ps128(odd);
    return _mm_add_ps(
        _mm_mul_ps(_mm_sub_ps(low_odd, low_even), _mm_set1_ps(fade_x)),
        low_even);
}

}  // namespace

__attribute__((target("avx2")))
void evaluate(const void *self, float *output, const Vec3 &position,
              int size_x, int size_y, int size_z, const Vec3 &scale,
              int step_x, int step_y, int step_z, float amplitude)
{
    const auto *object = static_cast<const unsigned char *>(self);
    const auto *permutation = reinterpret_cast<const int *>(object + 12);
    const float offset_y = read_float(object + 4);
    const float inverse = 1.0F / amplitude;
    const float initial_value = read_float(object + 0x810) * scale.y + offset_y;
    const int initial_y = static_cast<int>(initial_value);
    const float initial_fraction = initial_value - static_cast<float>(initial_y);
    std::array<int, 41> lattice_y_values;
    std::array<int, 41> truncated_y_values;
    std::array<float, 41> local_y_values;
    std::array<float, 41> fade_y_values;
    std::array<float, 41> gradient_y_values;
    for (int iy = 0; iy < size_y; ++iy) {
        const float full_y = (static_cast<float>(iy * step_y) + position.y) *
                             scale.y + offset_y;
        const float floor_y = ::floorf(full_y);
        const int truncated_y = static_cast<int>(full_y);
        const float local_y = full_y - floor_y;
        lattice_y_values[iy] =
            static_cast<unsigned char>(static_cast<int>(floor_y));
        truncated_y_values[iy] = truncated_y;
        local_y_values[iy] = local_y;
        gradient_y_values[iy] = truncated_y == initial_y
            ? initial_fraction
            : local_y - ::floorf(local_y / scale.y) * scale.y;
    }
    fade::array(fade_y_values.data(), local_y_values.data(), size_y);
    std::array<int, 5> lattice_z_values;
    std::array<float, 5> local_z_values;
    std::array<float, 5> fade_z_values;
    for (int iz = 0; iz < size_z; ++iz) {
        const float full_z = (static_cast<float>(iz * step_z) + position.z) *
                             scale.z + read_float(object + 8);
        const float floor_z = ::floorf(full_z);
        const float local_z = full_z - floor_z;
        lattice_z_values[iz] = static_cast<int>(floor_z);
        local_z_values[iz] = local_z;
    }
    fade::array(fade_z_values.data(), local_z_values.data(), size_z);
    int previous_y = -1;
    float corners[4]{};
    std::size_t index = 0;
    for (int ix = 0; ix < size_x; ++ix) {
        const float full_x = (static_cast<float>(ix * step_x) + position.x) *
                             scale.x + read_float(object);
        const float floor_x = ::floorf(full_x);
        const int lattice_x = static_cast<int>(floor_x);
        const float local_x = full_x - floor_x;
        const int x0 = permutation[static_cast<unsigned char>(lattice_x)];
        const int x1 = permutation[static_cast<unsigned char>(lattice_x + 1)];
        const float fade_x = fade::one(local_x);
        for (int iz = 0; iz < size_z; ++iz) {
            int iy = 0;
            while (iy < size_y) {
                const int lattice_y = lattice_y_values[iy];
                if (truncated_y_values[iy] == initial_y ||
                    lattice_y != previous_y) {
                    const auto corner_values = dots(
                        permutation, x0, x1, lattice_y, lattice_z_values[iz],
                        local_x, gradient_y_values[iy],
                        local_z_values[iz], fade_x);
                    _mm_storeu_ps(corners, corner_values);
                    previous_y = lattice_y;
                }
                int run_end = iy + 1;
                while (run_end < size_y &&
                       truncated_y_values[run_end] != initial_y &&
                       lattice_y_values[run_end] == previous_y) {
                    ++run_end;
                }
                const __m256 low_start = _mm256_set1_ps(corners[0]);
                const __m256 low_delta =
                    _mm256_set1_ps(corners[1] - corners[0]);
                const __m256 high_start = _mm256_set1_ps(corners[2]);
                const __m256 high_delta =
                    _mm256_set1_ps(corners[3] - corners[2]);
                const __m256 fade_z =
                    _mm256_set1_ps(fade_z_values[iz]);
                const __m256 inverse_values = _mm256_set1_ps(inverse);
                for (; iy + 8 <= run_end; iy += 8, index += 8) {
                    const __m256 fade_y =
                        _mm256_loadu_ps(fade_y_values.data() + iy);
                    const __m256 low = _mm256_add_ps(
                        _mm256_mul_ps(low_delta, fade_y), low_start);
                    const __m256 high = _mm256_add_ps(
                        _mm256_mul_ps(high_delta, fade_y), high_start);
                    const __m256 value = _mm256_add_ps(
                        _mm256_mul_ps(
                            _mm256_sub_ps(high, low), fade_z),
                        low);
                    const __m256 previous =
                        _mm256_loadu_ps(output + index);
                    _mm256_storeu_ps(
                        output + index,
                        _mm256_add_ps(
                            previous,
                            _mm256_mul_ps(value, inverse_values)));
                }
                for (; iy + 4 <= run_end; iy += 4, index += 4) {
                    const __m128 fade_y =
                        _mm_loadu_ps(fade_y_values.data() + iy);
                    const __m128 low = _mm_add_ps(
                        _mm_mul_ps(_mm256_castps256_ps128(low_delta), fade_y),
                        _mm256_castps256_ps128(low_start));
                    const __m128 high = _mm_add_ps(
                        _mm_mul_ps(_mm256_castps256_ps128(high_delta), fade_y),
                        _mm256_castps256_ps128(high_start));
                    const __m128 value = _mm_add_ps(
                        _mm_mul_ps(
                            _mm_sub_ps(high, low),
                            _mm256_castps256_ps128(fade_z)),
                        low);
                    _mm_storeu_ps(
                        output + index,
                        _mm_add_ps(
                            _mm_loadu_ps(output + index),
                            _mm_mul_ps(
                                value,
                                _mm256_castps256_ps128(inverse_values))));
                }
                const int remaining = run_end - iy;
                if (remaining >= 2) {
                    tail::add_two_or_three(
                        output, index, fade_y_values.data(), iy, remaining,
                        _mm256_castps256_ps128(low_delta),
                        _mm256_castps256_ps128(low_start),
                        _mm256_castps256_ps128(high_delta),
                        _mm256_castps256_ps128(high_start),
                        _mm256_castps256_ps128(fade_z),
                        _mm256_castps256_ps128(inverse_values));
                    iy = run_end;
                    index += static_cast<std::size_t>(remaining);
                } else if (iy < run_end) {
                    const float fade_y = fade_y_values[iy];
                    const float low =
                        (corners[1] - corners[0]) * fade_y + corners[0];
                    const float high =
                        (corners[3] - corners[2]) * fade_y + corners[2];
                    output[index++] +=
                        ((high - low) * fade_z_values[iz] + low) * inverse;
                    ++iy;
                }
            }
        }
    }
}

}  // namespace chunklet::noise::area
