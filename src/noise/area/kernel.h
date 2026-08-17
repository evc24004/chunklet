#pragma once

namespace chunklet::noise::area {

struct Vec3 {
    float x;
    float y;
    float z;
};

void evaluate(const void *self, float *output, const Vec3 &position,
              int size_x, int size_y, int size_z, const Vec3 &scale,
              int step_x, int step_y, int step_z, float amplitude);


}  // namespace chunklet::noise::area
