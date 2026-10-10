// Usage: g++ -std=c++20 -O2 -ffunction-sections -fdata-sections -Isrc
// tools/random_state_test.cpp -Wl,--gc-sections -o .artifacts/random_state_test
// Then run .artifacts/random_state_test. Includes the production comparator;
// linker GC discards native installation paths that require BDS symbols.
#include "noise/construction/direct.cpp"
#include <cstdio>
#include <stdexcept>

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    using namespace chunklet::noise::construction;
    const std::array<std::uintptr_t, 3> empty_output{};
    const std::array<unsigned char, 1> empty_candidate{};
    // Exact 24-byte objects make an accidental legacy-sized read detectable
    // under ASan. Only the vptr differs; both 64-bit RNG state words must match.
    std::array<unsigned char, 24> original_random{}, candidate_random{};
    candidate_random[0] = 1;
    for (int mode : {0, 1}) {
        require(equivalent(empty_output.data(), empty_candidate.data(), 0,
                           original_random.data(), candidate_random.data(), false, mode),
                "vptr is not random state");
        for (std::size_t offset = 8; offset < 24; ++offset) {
            candidate_random[offset] = 1;
            require(!equivalent(empty_output.data(), empty_candidate.data(), 0,
                                original_random.data(), candidate_random.data(), false, mode),
                    "Xoroshiro state-only divergence passed validation");
            candidate_random[offset] = 0;
        }
    }
    std::array<unsigned char, 0xa00> legacy{}, clone{};
    clone[0] = 1;
    clone[0x9f8] = 1;
    require(equivalent(empty_output.data(), empty_candidate.data(), 0,
                       legacy.data(), clone.data(), true, 0),
            "legacy vptr and cloned self-pointer must be excluded");
    for (std::size_t offset : {std::size_t{8}, std::size_t{0x9f7}}) {
        clone[offset] = 1;
        require(!equivalent(empty_output.data(), empty_candidate.data(), 0,
                            legacy.data(), clone.data(), true, 0),
                "legacy engine divergence passed validation");
        clone[offset] = 0;
    }
    std::puts("PASS: both Xoroshiro words checked in both modes; legacy state and pointer exclusions preserved");
}
