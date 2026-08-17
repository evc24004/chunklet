#pragma once

namespace chunklet::noise::perlin {

float evaluate(const void *noise, int x, int y, int z,
               float local_x, float local_y, float local_z);


}  // namespace chunklet::noise::perlin
