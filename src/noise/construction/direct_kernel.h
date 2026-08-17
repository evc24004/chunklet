#pragma once

#include <cstddef>

namespace chunklet::noise::construction::direct_kernel {

inline constexpr std::size_t kElementSize = 0x124;
inline constexpr std::size_t kMaximumOctaves = 31;

void construct_into(unsigned char *storage, void *random, int octave,
                    const float *amplitudes, std::size_t count);
bool construct_positional_into(unsigned char *storage, void *random, int octave,
                               const float *amplitudes, std::size_t count);

}  // namespace chunklet::noise::construction::direct_kernel
