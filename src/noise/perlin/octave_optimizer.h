#pragma once

#include <cstddef>

namespace chunklet::noise::perlin {

void install_octaves();
void remove_octaves() noexcept;
std::size_t octave_mismatch_count() noexcept;
std::size_t octave_validation_count() noexcept;

}  // namespace chunklet::noise::perlin
