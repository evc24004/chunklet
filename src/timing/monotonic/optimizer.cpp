#include "timing/monotonic/optimizer.h"

#include "native/layout.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <x86intrin.h>

namespace chunklet::timing::monotonic {
namespace {
constexpr std::uintptr_t kClock = 0x3cfeb60;
constexpr std::size_t kValidationCalls = 4'096;
constexpr std::uint64_t kMaximumAllowedErrorNs = 100'000;
constexpr std::array<unsigned char, 14> kOriginal{
    0x48, 0x83, 0xec, 0x18, 0x48, 0x8d, 0x74,
    0x24, 0x08, 0xbf, 0x01, 0x00, 0x00, 0x00};

using Clock = std::uint64_t (*)();
std::uintptr_t base;
void *trampoline;
Clock original;
bool installed;
std::uint64_t base_ticks;
std::uint64_t base_ns;
std::uint64_t ticks_per_second;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> mismatches{};
std::atomic<std::uint64_t> maximum_error{};

std::uint64_t read_ticks() noexcept
{
    unsigned int auxiliary;
    return __rdtscp(&auxiliary);
}

std::uint64_t kernel_tsc_frequency()
{
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (!line.starts_with("bogomips")) {
            continue;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            break;
        }
        const double bogomips = std::stod(line.substr(separator + 1));
        const auto frequency = static_cast<std::uint64_t>(
            std::llround(bogomips * 500'000.0));
        if (frequency >= 1'000'000'000ULL &&
            frequency <= 10'000'000'000ULL) {
            return frequency;
        }
        break;
    }
    throw std::runtime_error("Kernel did not expose a usable invariant TSC frequency");
}

std::uint64_t fast_clock() noexcept
{
    const auto elapsed_ticks = read_ticks() - base_ticks;
    const auto elapsed_ns = static_cast<std::uint64_t>(
        (static_cast<unsigned __int128>(elapsed_ticks) * 1'000'000'000ULL) /
        ticks_per_second);
    return base_ns + elapsed_ns;
}

void record_maximum(std::uint64_t error) noexcept
{
    auto observed = maximum_error.load(std::memory_order_relaxed);
    while (observed < error &&
           !maximum_error.compare_exchange_weak(
               observed, error, std::memory_order_relaxed)) {
    }
}

extern "C" __attribute__((noinline)) std::uint64_t checked_clock() noexcept
{
    const int state = validation_state.load(std::memory_order_acquire);
    if (state > 0) {
        return fast_clock();
    }
    const auto expected = original();
    if (state < 0) {
        return expected;
    }
    const auto candidate = fast_clock();
    const auto error = expected > candidate
        ? expected - candidate
        : candidate - expected;
    record_maximum(error);
    if (error > kMaximumAllowedErrorNs) {
        mismatches.fetch_add(1, std::memory_order_relaxed);
        validation_state.store(-1, std::memory_order_release);
        return expected;
    }
    const auto count = validations.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count >= kValidationCalls) {
        int validating = 0;
        validation_state.compare_exchange_strong(
            validating, 1, std::memory_order_release,
            std::memory_order_relaxed);
    }
    return expected;
}

std::pair<void *, std::size_t> target_page()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto begin = (base + kClock) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), page_size};
}

void create_trampoline()
{
    constexpr std::size_t size = kOriginal.size() + 13;
    trampoline = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) {
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(errno));
    }
    auto *code = static_cast<unsigned char *>(trampoline);
    std::memcpy(code, kOriginal.data(), kOriginal.size());
    std::size_t cursor = kOriginal.size();
    code[cursor++] = 0x49;
    code[cursor++] = 0xbb;
    const auto resume = base + kClock + kOriginal.size();
    std::memcpy(code + cursor, &resume, sizeof(resume));
    cursor += sizeof(resume);
    code[cursor++] = 0x41;
    code[cursor++] = 0xff;
    code[cursor] = 0xe3;
    if (mprotect(trampoline, size, PROT_READ | PROT_EXEC) != 0) {
        munmap(trampoline, size);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(errno));
    }
    original = reinterpret_cast<Clock>(trampoline);
}

void patch_target()
{
    std::array<unsigned char, kOriginal.size()> patch{0x48, 0xb8};
    const auto helper = reinterpret_cast<std::uintptr_t>(&checked_clock);
    std::memcpy(patch.data() + 2, &helper, sizeof(helper));
    patch[10] = 0xff;
    patch[11] = 0xe0;
    patch[12] = 0x90;
    patch[13] = 0x90;
    std::memcpy(reinterpret_cast<void *>(base + kClock),
                patch.data(), patch.size());
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    const auto *target = reinterpret_cast<const unsigned char *>(base + kClock);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS monotonic clock does not match the pinned build");
    }
    ticks_per_second = kernel_tsc_frequency();
    create_trampoline();
    const auto before = read_ticks();
    base_ns = original();
    const auto after = read_ticks();
    base_ticks = before + (after - before) / 2;
    validations.store(0, std::memory_order_relaxed);
    mismatches.store(0, std::memory_order_relaxed);
    maximum_error.store(0, std::memory_order_relaxed);
    validation_state.store(0, std::memory_order_relaxed);
    const auto [page, size] = target_page();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        remove();
        throw std::runtime_error(std::strerror(errno));
    }
    patch_target();
    __builtin___clear_cache(reinterpret_cast<char *>(base + kClock),
                            reinterpret_cast<char *>(base + kClock + kOriginal.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        std::memcpy(reinterpret_cast<void *>(base + kClock),
                    kOriginal.data(), kOriginal.size());
        mprotect(page, size, PROT_READ | PROT_EXEC);
        munmap(trampoline, kOriginal.size() + 13);
        trampoline = nullptr;
        original = nullptr;
        throw std::runtime_error(std::strerror(failure));
    }
    installed = true;
}

void remove() noexcept
{
    if (installed) {
        const auto [page, size] = target_page();
        if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            std::memcpy(reinterpret_cast<void *>(base + kClock),
                        kOriginal.data(), kOriginal.size());
            __builtin___clear_cache(reinterpret_cast<char *>(base + kClock),
                                    reinterpret_cast<char *>(base + kClock + kOriginal.size()));
            mprotect(page, size, PROT_READ | PROT_EXEC);
        }
        installed = false;
    }
    if (trampoline != nullptr) {
        munmap(trampoline, kOriginal.size() + 13);
        trampoline = nullptr;
        original = nullptr;
    }
}

std::size_t validation_count() noexcept
{
    return validations.load(std::memory_order_relaxed);
}

std::uint64_t maximum_error_ns() noexcept
{
    return maximum_error.load(std::memory_order_relaxed);
}

std::size_t mismatch_count() noexcept
{
    return mismatches.load(std::memory_order_relaxed);
}

}  // namespace chunklet::timing::monotonic
