#pragma once

#include <cstddef>

namespace chunklet::noise::interpolation {

void install();
void remove() noexcept;
std::size_t mismatch_count() noexcept;

}  // namespace chunklet::noise::interpolation
