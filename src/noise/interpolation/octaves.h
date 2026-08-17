#pragma once

namespace chunklet::noise::interpolation::octaves {

float evaluate(const unsigned char *begin, const unsigned char *end,
               float x, float y, float z);
float evaluate_cached(const unsigned char *begin, const unsigned char *end,
                      float x, float y, float z);

}  // namespace chunklet::noise::interpolation::octaves
