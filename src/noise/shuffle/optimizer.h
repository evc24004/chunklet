#pragma once

namespace chunklet::noise::shuffle {
void shuffle_values(void *random, unsigned char *values);

void install();
void remove() noexcept;

}  // namespace chunklet::noise::shuffle
