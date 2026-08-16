#pragma once

namespace chunklet::noise::interpolation {

float sample(const void *noise, int x, int y, int z,
             float local_x, float local_y, float local_z);

}  // namespace chunklet::noise::interpolation
