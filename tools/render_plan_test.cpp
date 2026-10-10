// Usage: g++ -std=c++20 -Isrc tools/render_plan_test.cpp src/render/plan.cpp
//        -o .artifacts/render_plan_test && .artifacts/render_plan_test
// Regression: the radius-256 negative-coordinate tile stopped at 992/1024
// when min-Z/max-X/max-Z had only five dependency columns instead of six.
#include "render/plan.h"
#include <cstdio>
#include <stdexcept>

void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    using namespace chunklet::render;
    for (const auto center : {-4752, 0, 4752}) {
        const auto target = square_bounds(center, center, 256);
        const auto generation = generation_bounds(target);
        require(target.count() == 1024, "target changed");
        require(generation.count() == 1936, "radius-256 must request its complete 44x44 dependency footprint");
        for (const auto position : row_major_order(target)) {
            require(generation.contains({position.x - 6, position.z - 6}), "missing minimum dependency frontier");
            require(generation.contains({position.x + 6, position.z + 6}), "missing maximum dependency frontier");
        }
        const auto ordered = morton_order(generation);
        require(ordered.size() == generation.count(), "Morton ordering dropped a dependency");
    }
    std::puts("PASS: every radius-256 target retains all six dependency columns on every side");
}
