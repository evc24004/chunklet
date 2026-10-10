#pragma once

#include <cstdint>
#include <stdexcept>

namespace chunklet::timing::monotonic {

struct ClockScale {
    std::uint64_t frequency;
    std::uint64_t reciprocal;
};

inline ClockScale make_clock_scale(std::uint64_t frequency)
{
    constexpr std::uint64_t billion = 1'000'000'000ULL;
    if (frequency < billion || frequency > 10 * billion) {
        throw std::invalid_argument("TSC frequency is outside the verified scaling range");
    }
    // At exactly 1 GHz the ratio is one; represent it separately because its
    // Q64 reciprocal would be 2^64 and would not fit in uint64_t.
    const auto reciprocal = frequency == billion ? 0 : static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(billion) << 64) / frequency);
    return {frequency, reciprocal};
}

inline std::uint64_t scale_ticks(std::uint64_t ticks, ClockScale scale) noexcept
{
    constexpr std::uint64_t billion = 1'000'000'000ULL;
    if (scale.frequency == billion) {
        return ticks;
    }
    // q=floor(2^64*1e9/f): floor(t*q/2^64) underestimates floor(t*1e9/f)
    // by at most one for every uint64_t t. One multiplication comparison
    // restores that last unit exactly; no division remains in the hot path.
    const auto approximate = static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(ticks) * scale.reciprocal) >> 64);
    // f>1e9 means approximate<UINT64_MAX, so approximate+1 cannot overflow.
    // Both products fit in 128 bits (at most 98 significant bits).
    const bool round_up = static_cast<unsigned __int128>(approximate + 1) * scale.frequency <=
                          static_cast<unsigned __int128>(ticks) * billion;
    return approximate + round_up;
}

}  // namespace chunklet::timing::monotonic
