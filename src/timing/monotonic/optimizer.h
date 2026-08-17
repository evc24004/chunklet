#pragma once

#include <cstddef>
#include <cstdint>

namespace chunklet::timing::monotonic {

void install();
void remove() noexcept;
std::size_t validation_count() noexcept;
std::uint64_t maximum_error_ns() noexcept;
std::size_t mismatch_count() noexcept;

}  // namespace chunklet::timing::monotonic
