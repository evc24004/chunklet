#pragma once

#include <cstdint>

namespace chunklet::spatial::containment {

void compact_tables(const float *dense, const float *local) noexcept;
double evaluate(const void *source, std::uint64_t packed_xy, int z) noexcept;

}  // namespace chunklet::spatial::containment
