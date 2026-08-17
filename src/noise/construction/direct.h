#pragma once

#include <cstdint>


namespace chunklet::noise::construction {
struct DirectStats {
    std::uint64_t validations;
    std::uint64_t mismatches;
    std::uint64_t calls;
    std::uint64_t mode_zero;
    std::uint64_t random_matches;
    std::uint64_t shape_matches;
    std::uint64_t next_int;
    std::uint64_t next_double;
    std::uint64_t consume;
    std::uint64_t mismatch_mode;
    std::uint64_t mismatch_element;
    std::uint64_t mismatch_offset;
    std::uint64_t mismatch_expected;
    std::uint64_t mismatch_actual;
};

void install_direct();
void remove_direct() noexcept;
DirectStats direct_stats() noexcept;

}  // namespace chunklet::noise::construction
