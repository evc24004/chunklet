#include "noise/perlin/kernel.h"
#include "noise/perlin/evaluator.h"

namespace chunklet::noise::perlin {

__attribute__((target("avx2")))
float evaluate(const void *noise, int x, int y, int z,
               float local_x, float local_y, float local_z)
{
    return detail::evaluate_sample(
        noise, x, y, z, local_x, local_y, local_z);
}

}  // namespace chunklet::noise::perlin
