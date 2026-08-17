#include "noise/perlin/optimizer.h"

#include "native/layout.h"
#include "noise/perlin/kernel.h"

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::noise::perlin {
namespace {
constexpr std::uintptr_t kSample = 0xc9ca690;
constexpr std::array<unsigned char, 16> kOriginal{
    0x41, 0x57, 0x41, 0x56, 0x53, 0x40, 0x0f, 0xb6,
    0xc6, 0x0f, 0xb6, 0x44, 0x07, 0x0c, 0xff, 0xc6};
constexpr std::size_t kValidationCalls = 32;
constexpr std::size_t kTrampolineSize = kOriginal.size() + 14;
using Sample = float (*)(const void *, int, int, int, float, float, float);

std::uintptr_t base;
void *trampoline;
Sample original;
bool installed;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> validation_claims{};
std::atomic<Sample> active_sample{};
std::atomic<std::size_t> mismatches{};
std::array<unsigned, 8> corner_indices(
    const void *noise, int x, int y, int z)
{
    const auto *p = static_cast<const unsigned char *>(noise) + 12;
    const int x0 = p[static_cast<unsigned char>(x)];
    const int x1 = p[static_cast<unsigned char>(x + 1)];
    const int xy00 = p[static_cast<unsigned char>(y + x0)];
    const int xy01 = p[static_cast<unsigned char>(y + x0 + 1)];
    const int xy10 = p[static_cast<unsigned char>(y + x1)];
    const int xy11 = p[static_cast<unsigned char>(y + x1 + 1)];
    return {
        p[static_cast<unsigned char>(z + xy00)] & 15U,
        p[static_cast<unsigned char>(z + xy10)] & 15U,
        p[static_cast<unsigned char>(z + xy01)] & 15U,
        p[static_cast<unsigned char>(z + xy11)] & 15U,
        p[static_cast<unsigned char>(z + xy00 + 1)] & 15U,
        p[static_cast<unsigned char>(z + xy10 + 1)] & 15U,
        p[static_cast<unsigned char>(z + xy01 + 1)] & 15U,
        p[static_cast<unsigned char>(z + xy11 + 1)] & 15U};
}


extern "C" float checked_sample(const void *noise, int x, int y, int z,
                                float local_x, float local_y, float local_z)
{
    const int state = validation_state.load(std::memory_order_acquire);
    if (state > 0) {
        return evaluate(noise, x, y, z, local_x, local_y, local_z);
    }
    if (state < 0) {
        return original(noise, x, y, z, local_x, local_y, local_z);
    }
    if (validation_claims.fetch_add(1, std::memory_order_relaxed) >=
        kValidationCalls) {
        return original(noise, x, y, z, local_x, local_y, local_z);
    }
    const float expected =
        original(noise, x, y, z, local_x, local_y, local_z);
    const float actual =
        evaluate(noise, x, y, z, local_x, local_y, local_z);
    if (std::bit_cast<std::uint32_t>(actual) !=
        std::bit_cast<std::uint32_t>(expected)) {
        const auto hashes = corner_indices(noise, x, y, z);
        std::fprintf(
            stderr,
            "[Chunklet] Perlin mismatch xyz=%d,%d,%d local=%a,%a,%a expected=%08x actual=%08x\n",
            x, y, z, local_x, local_y, local_z,
            std::bit_cast<std::uint32_t>(expected),
            std::bit_cast<std::uint32_t>(actual));
        std::fprintf(
            stderr, "[Chunklet] Perlin hashes=%u,%u,%u,%u,%u,%u,%u,%u\n",
            hashes[0], hashes[1], hashes[2], hashes[3],
            hashes[4], hashes[5], hashes[6], hashes[7]);
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
            active_sample.store(&evaluate, std::memory_order_release);
        }
    }
    return expected;
}

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + kSample;
    const auto begin = target & ~(page_size - 1);
    const auto end =
        (target + kOriginal.size() + page_size - 1) & ~(page_size - 1);
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
    const auto resume = base + kSample + kOriginal.size();
    std::memcpy(code + cursor, &resume, sizeof(resume));
    if (mprotect(trampoline, kTrampolineSize, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(failure));
    }
    original = reinterpret_cast<Sample>(trampoline);
}

void write_patch(bool optimize)
{
    auto *target = reinterpret_cast<unsigned char *>(base + kSample);
    if (!optimize) {
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        return;
    }
    std::array<unsigned char, kOriginal.size()> patch{};
    patch.fill(0x90);
    patch[0] = 0x48;
    patch[1] = 0xb8;
    const auto dispatch = reinterpret_cast<std::uintptr_t>(&active_sample);
    std::memcpy(patch.data() + 2, &dispatch, sizeof(dispatch));
    patch[10] = 0xff;
    patch[11] = 0x20;
    std::memcpy(target, patch.data(), patch.size());
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    if (!__builtin_cpu_supports("avx2")) {
        throw std::runtime_error("AVX2 perlin sampler requires AVX2");
    }
    base = native::executable_base();
    const auto *target =
        reinterpret_cast<const unsigned char *>(base + kSample);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS perlin sampler does not match the pinned build");
    }
    validation_state.store(0, std::memory_order_relaxed);
    validation_claims.store(0, std::memory_order_relaxed);
    active_sample.store(&checked_sample, std::memory_order_relaxed);
    validations.store(0, std::memory_order_relaxed);
    mismatches.store(0, std::memory_order_relaxed);
    create_trampoline();
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        remove();
        throw std::runtime_error(std::strerror(errno));
    }
    write_patch(true);
    __builtin___clear_cache(
        reinterpret_cast<char *>(base + kSample),
        reinterpret_cast<char *>(base + kSample + kOriginal.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        write_patch(false);
        __builtin___clear_cache(
            reinterpret_cast<char *>(base + kSample),
            reinterpret_cast<char *>(base + kSample + kOriginal.size()));
        mprotect(page, size, PROT_READ | PROT_EXEC);
        remove();
        throw std::runtime_error(std::strerror(failure));
    }
    installed = true;
}

void remove() noexcept
{
    if (installed) {
        const auto [page, size] = target_pages();
        if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            write_patch(false);
            __builtin___clear_cache(
                reinterpret_cast<char *>(base + kSample),
                reinterpret_cast<char *>(base + kSample + kOriginal.size()));
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

std::size_t mismatch_count() noexcept
{
    return mismatches.load(std::memory_order_relaxed);
}

std::size_t validation_count() noexcept
{
    return validations.load(std::memory_order_relaxed);
}

}  // namespace chunklet::noise::perlin
