// Usage: g++ -O3 -std=c++20 -Isrc tools/clock_scale_test.cpp
//        -o .artifacts/clock_scale_test && .artifacts/clock_scale_test
// Compare the optimized production scaler against independent 128-bit division
// at frequency bounds, tick overflow boundaries, and one million random inputs.
#include "timing/monotonic/scale.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

using namespace chunklet::timing::monotonic;

void check(std::uint64_t ticks, std::uint64_t frequency)
{
    const auto expected = static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(ticks) * 1'000'000'000ULL) / frequency);
    const auto scale = make_clock_scale(frequency);
    const auto actual = scale_ticks(ticks, scale);
    if (actual != expected) throw std::runtime_error("clock scaling differs from exact division");
    if (ticks != UINT64_MAX && scale_ticks(ticks + 1, scale) < actual)
        throw std::runtime_error("clock scaling is not monotonic");
}

int main()
{
    for (const auto frequency : {1'000'000'000ULL, 1'000'000'001ULL, 2'500'000'000ULL,
                                3'000'000'000ULL, 3'999'999'999ULL, 10'000'000'000ULL}) {
        for (const auto ticks : {0ULL, 1ULL, 1'000'000'000ULL, (1ULL << 63), UINT64_MAX - 1ULL, UINT64_MAX + 0ULL})
            check(ticks, frequency);
    }
    std::uint64_t random = 0x243f6a8885a308d3ULL;
    for (std::size_t iteration = 0; iteration < 1'000'000; ++iteration) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        const auto frequency = 1'000'000'000ULL + random % 9'000'000'001ULL;
        const auto ticks = random * 0x9e3779b97f4a7c15ULL;
        check(ticks, frequency);
    }
    for (const auto frequency : {0ULL, 999'999'999ULL, 10'000'000'001ULL, UINT64_MAX + 0ULL}) {
        bool rejected = false;
        try { (void)make_clock_scale(frequency); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("unsupported TSC frequency accepted");
    }
    std::puts("PASS: 1,000,036 exact conversions and adjacent-tick monotonicity checks; invalid frequencies rejected");
}
