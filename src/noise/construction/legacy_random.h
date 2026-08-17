#pragma once

#include <cstdint>

namespace chunklet::noise::construction::legacy_random {

void configure(std::uintptr_t executable_base) noexcept;
bool matches(void *random) noexcept;
double next_double(void *random) noexcept;
void shuffle(void *random, unsigned char *values) noexcept;
void consume(void *random, int count) noexcept;

}  // namespace chunklet::noise::construction::legacy_random
