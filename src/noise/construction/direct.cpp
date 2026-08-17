#include "noise/construction/direct.h"

#include "noise/construction/direct_runtime.h"
#include "noise/construction/direct_kernel.h"
#include "noise/construction/legacy_random.h"
#include "noise/construction/reserve.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chunklet::noise::construction {
namespace {
using direct_kernel::construct_into;
using direct_kernel::construct_positional_into;
using direct_kernel::kElementSize;
using direct_kernel::kMaximumOctaves;

constexpr std::uintptr_t kNextInt = 0x45419a0;
constexpr std::size_t kLegacyRandomSize = 0x9f0;
constexpr int kRequiredValidations = 8;

std::uintptr_t executable_base;
Constructor original_constructor;
std::atomic<int> state{};
std::atomic<int> positional_state{};
std::atomic<std::uint64_t> validations{};
std::atomic<std::uint64_t> mismatches{};
std::atomic<std::uint64_t> mode_zero_validations{};
std::atomic<std::uint64_t> positional_validations{};
std::atomic<std::uint64_t> calls{};
std::atomic<std::uint64_t> mode_zero{};
std::atomic<std::uint64_t> random_matches{};
std::atomic<std::uint64_t> shape_matches{};
std::atomic<std::uint64_t> observed_next_int{};
std::atomic<std::uint64_t> observed_next_double{};
std::atomic<std::uint64_t> observed_consume{};
std::atomic<std::uint64_t> mismatch_mode{};
std::atomic<std::uint64_t> mismatch_element{};
std::atomic<std::uint64_t> mismatch_offset{};
std::atomic<std::uint64_t> mismatch_expected{};
std::atomic<std::uint64_t> mismatch_actual{};
thread_local std::array<unsigned char, kMaximumOctaves * kElementSize>
    validation_output;
thread_local std::array<unsigned char, kLegacyRandomSize> validation_random;


void record_mismatch(int mode, std::size_t element, std::size_t offset,
                     std::uint64_t expected, std::uint64_t actual)
{
    mismatch_mode.store(static_cast<std::uint64_t>(mode), std::memory_order_relaxed);
    mismatch_element.store(element, std::memory_order_relaxed);
    mismatch_offset.store(offset, std::memory_order_relaxed);
    mismatch_expected.store(expected, std::memory_order_relaxed);
    mismatch_actual.store(actual, std::memory_order_relaxed);
}

bool equivalent(const void *output, const unsigned char *candidate,
                std::size_t count, const void *random,
                const void *candidate_random, bool compare_random, int mode)
{
    const auto *vector = static_cast<const std::uintptr_t *>(output);
    const auto bytes = vector[1] - vector[0];
    if (bytes != count * kElementSize) {
        record_mismatch(mode, 0, kElementSize, count * kElementSize, bytes);
        return false;
    }
    if (compare_random &&
        std::memcmp(static_cast<const unsigned char *>(random) + 8,
                    static_cast<const unsigned char *>(candidate_random) + 8,
                    kLegacyRandomSize - 8) != 0) {
        record_mismatch(mode, 0, kElementSize + 1, 0, 0);
        return false;
    }
    const auto *reference = reinterpret_cast<const unsigned char *>(vector[0]);
    for (std::size_t index = 0; index < count; ++index) {
        const auto *expected = reference + index * kElementSize;
        const auto *actual = candidate + index * kElementSize;
        if (expected[0x120] != actual[0x120]) {
            record_mismatch(mode, index, 0x120, expected[0x120], actual[0x120]);
            return false;
        }
        const auto compared = expected[0x120] == 0 ? std::size_t{1} : 0x121;
        for (std::size_t offset = 0; offset < compared; ++offset) {
            if (expected[offset] != actual[offset]) {
                record_mismatch(
                    mode, index, offset, expected[offset], actual[offset]);
                return false;
            }
        }
    }
    return true;
}

bool eligible(void *, int octave, std::size_t count, int mode)
{
    return (mode == 0 || mode == 1) && octave <= 0 && octave >= -64 &&
           count != 0 && count <= kMaximumOctaves;
}

bool run_candidate(void *output, void *random, int octave,
                   const float *amplitudes, std::size_t count, int mode)
{
    if (mode == 1 &&
        !construct_positional_into(validation_output.data(), random, octave,
                                   amplitudes, count)) {
        return false;
    }
    allocate_output(output, count);
    auto *vector = static_cast<std::uintptr_t *>(output);
    if (vector[0] == 0) {
        return false;
    }
    auto *storage = reinterpret_cast<unsigned char *>(vector[0]);
    if (mode == 0) {
        construct_into(storage, random, octave, amplitudes, count);
    } else {
        std::memcpy(storage, validation_output.data(), count * kElementSize);
    }
    vector[1] = vector[2];
    return true;
}
}  // namespace

extern "C" void direct_constructor(
    void *output, void *random, int octave, const void *amplitude_vector, int mode)
{
    const auto *words = static_cast<const std::uintptr_t *>(amplitude_vector);
    const auto bytes = words[1] - words[0];
    const auto count = bytes / sizeof(float);
    const auto *amplitudes = reinterpret_cast<const float *>(words[0]);
    auto **vtable = *reinterpret_cast<void ***>(random);
    calls.fetch_add(1, std::memory_order_relaxed);
    if (mode == 0) {
        mode_zero.fetch_add(1, std::memory_order_relaxed);
        std::uint64_t expected = 0;
        observed_next_int.compare_exchange_strong(
            expected, reinterpret_cast<std::uintptr_t>(vtable[3]) - executable_base,
            std::memory_order_relaxed);
        expected = 0;
        observed_next_double.compare_exchange_strong(
            expected, reinterpret_cast<std::uintptr_t>(vtable[7]) - executable_base,
            std::memory_order_relaxed);
        expected = 0;
        observed_consume.compare_exchange_strong(
            expected, reinterpret_cast<std::uintptr_t>(vtable[9]) - executable_base,
            std::memory_order_relaxed);
    }
    if (reinterpret_cast<std::uintptr_t>(vtable[3]) ==
        executable_base + kNextInt) {
        random_matches.fetch_add(1, std::memory_order_relaxed);
    }
    if (octave <= 0 && octave >= -64 && count != 0 &&
        count <= kMaximumOctaves) {
        shape_matches.fetch_add(1, std::memory_order_relaxed);
    }
    if (bytes % sizeof(float) != 0 || !eligible(random, octave, count, mode)) {
        original_constructor(output, random, octave, amplitude_vector, mode);
        return;
    }
    auto &candidate_state = mode == 0 ? state : positional_state;
    if (candidate_state.load(std::memory_order_acquire) == 1) {
        if (!run_candidate(output, random, octave, amplitudes, count, mode)) {
            candidate_state.store(-1, std::memory_order_release);
            original_constructor(output, random, octave, amplitude_vector, mode);
        }
        return;
    }

    std::memcpy(validation_random.data(), random, validation_random.size());
    bool positional_built = true;
    if (mode == 1) {
        positional_built = construct_positional_into(
            validation_output.data(), validation_random.data(), octave, amplitudes,
            count);
    }
    original_constructor(output, random, octave, amplitude_vector, mode);
    if (candidate_state.load(std::memory_order_acquire) < 0) {
        return;
    }
    const bool built =
        mode == 0
            ? (construct_into(validation_output.data(), validation_random.data(),
                              octave, amplitudes, count),
               true)
            : positional_built;
    if (!built ||
        !equivalent(output, validation_output.data(), count, random,
                    validation_random.data(), mode == 0, mode)) {
        mismatches.fetch_add(1, std::memory_order_relaxed);
        candidate_state.store(-1, std::memory_order_release);
        return;
    }
    validations.fetch_add(1, std::memory_order_relaxed);
    auto &mode_validations =
        mode == 0 ? mode_zero_validations : positional_validations;
    const auto completed =
        mode_validations.fetch_add(1, std::memory_order_relaxed) + 1;
    if (completed >= kRequiredValidations) {
        int expected = 0;
        candidate_state.compare_exchange_strong(
            expected, 1, std::memory_order_release, std::memory_order_relaxed);
    }
}

void configure_direct(std::uintptr_t base, Constructor original) noexcept
{
    executable_base = base;
    legacy_random::configure(base);
    original_constructor = original;
    state.store(0, std::memory_order_relaxed);
    positional_state.store(0, std::memory_order_relaxed);
}

DirectStats direct_stats() noexcept
{
    return {
        validations.load(std::memory_order_relaxed),
        mismatches.load(std::memory_order_relaxed),
        calls.load(std::memory_order_relaxed),
        mode_zero.load(std::memory_order_relaxed),
        random_matches.load(std::memory_order_relaxed),
        shape_matches.load(std::memory_order_relaxed),
        observed_next_int.load(std::memory_order_relaxed),
        observed_next_double.load(std::memory_order_relaxed),
        observed_consume.load(std::memory_order_relaxed),
        mismatch_mode.load(std::memory_order_relaxed),
        mismatch_element.load(std::memory_order_relaxed),
        mismatch_offset.load(std::memory_order_relaxed),
        mismatch_expected.load(std::memory_order_relaxed),
        mismatch_actual.load(std::memory_order_relaxed),
    };
}

}  // namespace chunklet::noise::construction
