// Usage: g++ -O3 -std=c++20 -Isrc tools/clock_scale_benchmark.cpp
//        -o .artifacts/clock_scale_benchmark
//        .artifacts/clock_scale_benchmark [frequency_hz=3000000000] [calls=10000000]
// Measures identical deterministic tick samples with native division versus
// the exact production reciprocal scaler. Reports median of five alternated
// rounds, nanoseconds/call and ratio. Include RDTSCP with final argument 'tsc'.
#include "timing/monotonic/scale.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <x86intrin.h>

using chunklet::timing::monotonic::ClockScale;
std::uint64_t benchmark_sink;

__attribute__((noinline)) std::uint64_t division(std::uint64_t ticks, ClockScale scale)
{
    return static_cast<std::uint64_t>((static_cast<unsigned __int128>(ticks) * 1'000'000'000ULL) / scale.frequency);
}

__attribute__((noinline)) std::uint64_t reciprocal(std::uint64_t ticks, ClockScale scale)
{
    return chunklet::timing::monotonic::scale_ticks(ticks, scale);
}

double measure(std::uint64_t (*convert)(std::uint64_t, ClockScale), ClockScale scale,
               std::uint64_t calls, bool with_tsc)
{
    std::uint64_t sum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < calls; ++index) {
        unsigned auxiliary;
        const auto ticks = with_tsc ? __rdtscp(&auxiliary) : index * 0x9e3779b97f4a7c15ULL;
        sum += convert(ticks, scale);
    }
    const auto finish = std::chrono::steady_clock::now();
    benchmark_sink = sum;
    return std::chrono::duration<double, std::nano>(finish - start).count() / calls;
}

int main(int argc, char **argv)
{
    const auto frequency = argc > 1 ? std::stoull(argv[1]) : 3'000'000'000ULL;
    const auto calls = argc > 2 ? std::stoull(argv[2]) : 10'000'000ULL;
    const bool with_tsc = argc > 3 && std::string_view(argv[3]) == "tsc";
    if (calls == 0) throw std::invalid_argument("calls must be positive");
    const auto scale = chunklet::timing::monotonic::make_clock_scale(frequency);
    std::array<double, 5> before{}, after{};
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (i % 2 == 0) {
            before[i] = measure(division, scale, calls, with_tsc);
            after[i] = measure(reciprocal, scale, calls, with_tsc);
        } else {
            after[i] = measure(reciprocal, scale, calls, with_tsc);
            before[i] = measure(division, scale, calls, with_tsc);
        }
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    std::printf("frequency_hz=%llu calls=%llu tsc=%d division_ns=%.3f reciprocal_ns=%.3f speedup=%.3f checksum=%llu\n",
                static_cast<unsigned long long>(frequency), static_cast<unsigned long long>(calls), with_tsc,
                before[2], after[2], before[2] / after[2], static_cast<unsigned long long>(benchmark_sink));
}
