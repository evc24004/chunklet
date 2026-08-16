#include "render/plan.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <unordered_set>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    using chunklet::render::ChunkBounds;
    using chunklet::render::ChunkPosition;
    using chunklet::render::center_out;
    using chunklet::render::chunk_key;
    using chunklet::render::square_bounds;

    const auto benchmark = square_bounds(0, 0, 500);
    require(benchmark.min_x == -32 && benchmark.min_z == -32 &&
                benchmark.max_x == 31 && benchmark.max_z == 31,
            "1,000 block square must align to chunks -32 through 31");
    require(benchmark.count() == 4096, "benchmark square must contain 4,096 chunks");
    const auto generation = benchmark.expanded(8);
    require(generation.count() == 6400,
            "benchmark generation bounds must contain 6,400 chunks");
    require(generation.contains({-40, -40}) && generation.contains({39, 39}),
            "generation border must expand each benchmark edge by eight chunks");

    const auto single_negative = square_bounds(-16, -16, 1);
    require(single_negative.min_x == -2 && single_negative.max_x == -1,
            "negative block bounds must use floor division");

    const auto positions = center_out(benchmark);
    require(positions.size() == benchmark.count(), "planner must return every target chunk");
    require(positions.front() == ChunkPosition{-1, -1},
            "planner must start at the deterministic center chunk");
    require(benchmark.contains(positions.back()), "planner must not leave the target bounds");

    std::unordered_set<std::uint64_t> unique;
    for (const auto position : positions) {
        unique.insert(chunk_key(position));
    }
    require(unique.size() == positions.size(), "planner must not return duplicate chunks");
    require(chunk_key({-1, 0}) != chunk_key({0, -1}),
            "signed chunk coordinates must have distinct keys");

    std::cout << "All planner contracts passed.\n";
    return 0;
}
