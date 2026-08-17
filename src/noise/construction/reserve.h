#pragma once
#include <cstddef>


namespace chunklet::noise::construction {
void allocate_output(void *output, std::size_t count);

void install();
void remove() noexcept;

}  // namespace chunklet::noise::construction
