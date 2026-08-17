#include "noise/perlin/octave_optimizer.h"

#include "native/layout.h"
#include "noise/perlin/octaves.h"

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::noise::perlin {
namespace {
constexpr std::uintptr_t kEvaluate = 0xc9ca3a0;
constexpr std::array<unsigned char, 14> kOriginal{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x48};
constexpr std::size_t kValidationCalls = 32;
constexpr std::size_t kTrampolineSize = kOriginal.size() + 14;
using Evaluate = float (*)(const void *, float, float, float);

std::uintptr_t base;
void *trampoline;
Evaluate original;
bool installed;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> validation_claims{};
std::atomic<Evaluate> active_octaves{};
std::atomic<std::size_t> mismatches{};


extern "C" float checked_octaves(
    const void *self, float x, float y, float z)
{
    const int state = validation_state.load(std::memory_order_acquire);
    if (state > 0) {
        return evaluate_octaves(self, x, y, z);
    }
    if (state < 0) {
        return original(self, x, y, z);
    }
    if (validation_claims.fetch_add(1, std::memory_order_relaxed) >=
        kValidationCalls) {
        return original(self, x, y, z);
    }
    const float expected = original(self, x, y, z);
    const float actual = evaluate_octaves(self, x, y, z);
    if (std::bit_cast<std::uint32_t>(actual) !=
        std::bit_cast<std::uint32_t>(expected)) {
        mismatches.fetch_add(1, std::memory_order_relaxed);
        validation_state.store(-1, std::memory_order_release);
        return expected;
    }
    if (validations.fetch_add(1, std::memory_order_acq_rel) + 1 ==
        kValidationCalls) {
        int validating = 0;
        if (validation_state.compare_exchange_strong(
                validating, 1, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            active_octaves.store(
                &evaluate_octaves, std::memory_order_release);
        }
    }
    return expected;
}

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto begin = (base + kEvaluate) & ~(page_size - 1);
    const auto end = (base + kEvaluate + kOriginal.size() + page_size - 1) &
                     ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), end - begin};
}

void create_trampoline()
{
    trampoline = mmap(nullptr, kTrampolineSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) {
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(errno));
    }
    auto *code = static_cast<unsigned char *>(trampoline);
    std::memcpy(code, kOriginal.data(), kOriginal.size());
    std::size_t cursor = kOriginal.size();
    code[cursor++] = 0xff;
    code[cursor++] = 0x25;
    std::memset(code + cursor, 0, sizeof(std::uint32_t));
    cursor += sizeof(std::uint32_t);
    const auto resume = base + kEvaluate + kOriginal.size();
    std::memcpy(code + cursor, &resume, sizeof(resume));
    if (mprotect(trampoline, kTrampolineSize, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(failure));
    }
    original = reinterpret_cast<Evaluate>(trampoline);
}

void write_patch(bool optimize)
{
    auto *target = reinterpret_cast<unsigned char *>(base + kEvaluate);
    if (!optimize) {
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        return;
    }
    std::array<unsigned char, kOriginal.size()> patch{};
    patch[0] = 0x48;
    patch[1] = 0xb8;
    const auto dispatch = reinterpret_cast<std::uintptr_t>(&active_octaves);
    std::memcpy(patch.data() + 2, &dispatch, sizeof(dispatch));
    patch[10] = 0xff;
    patch[11] = 0x20;
    std::memcpy(target, patch.data(), patch.size());
}
}  // namespace

void install_octaves()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    const auto *target = reinterpret_cast<const unsigned char *>(
        base + kEvaluate);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error(
            "BDS octave sampler does not match the pinned build");
    }
    validation_state.store(0, std::memory_order_relaxed);
    validation_claims.store(0, std::memory_order_relaxed);
    active_octaves.store(&checked_octaves, std::memory_order_relaxed);
    validations.store(0, std::memory_order_relaxed);
    mismatches.store(0, std::memory_order_relaxed);
    create_trampoline();
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        remove_octaves();
        throw std::runtime_error(std::strerror(errno));
    }
    write_patch(true);
    __builtin___clear_cache(static_cast<char *>(page),
                            static_cast<char *>(page) + size);
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        write_patch(false);
        mprotect(page, size, PROT_READ | PROT_EXEC);
        remove_octaves();
        throw std::runtime_error(std::strerror(failure));
    }
    installed = true;
}

void remove_octaves() noexcept
{
    if (installed) {
        const auto [page, size] = target_pages();
        if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            write_patch(false);
            __builtin___clear_cache(static_cast<char *>(page),
                                    static_cast<char *>(page) + size);
            mprotect(page, size, PROT_READ | PROT_EXEC);
        }
        installed = false;
    }
    if (trampoline != nullptr) {
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        original = nullptr;
    }
}

std::size_t octave_mismatch_count() noexcept
{
    return mismatches.load(std::memory_order_relaxed);
}

std::size_t octave_validation_count() noexcept
{
    return validations.load(std::memory_order_relaxed);
}

}  // namespace chunklet::noise::perlin
