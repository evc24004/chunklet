#pragma once

#include <cstddef>

namespace chunklet::spatial::containment {

void install();
void remove() noexcept;
std::size_t mismatch_count() noexcept;
std::size_t validation_count() noexcept;

}  // namespace chunklet::spatial::containment
