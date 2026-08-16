#include "noise/interpolation/optimizer.h"

#include "native/layout.h"
#include "noise/interpolation/kernel.h"

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::noise::interpolation {
namespace {
constexpr std::uintptr_t kMultiOctaveSample = 0xc9ca3a0;
constexpr std::size_t kValidationCalls = 4'096;
constexpr std::array<unsigned char, 14> kOriginal{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x48};
constexpr std::size_t kNoiseSize = 0x124;

using MultiOctaveSample = float (*)(const void *, float, float, float);
std::uintptr_t base;
void *trampoline;
MultiOctaveSample original;
bool installed;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> mismatches{};

float read_float(const unsigned char *address)
{
    float value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

const unsigned char *read_pointer(const unsigned char *address)
{
    const unsigned char *value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

__attribute__((target("avx2")))
float evaluate_noises(const unsigned char *begin, const unsigned char *end,
                      float x, float y, float z)
{
    float result = 0.0F;
    for (auto *noise = begin; noise != end; noise += kNoiseSize) {
        if (noise[0x120] != 1) {
            continue;
        }
        const float frequency = read_float(noise + 0x114);
        const float sample_x = x * frequency + read_float(noise);
        const float sample_y = y * frequency + read_float(noise + 4);
        const float sample_z = z * frequency + read_float(noise + 8);
        const int floor_x = static_cast<int>(::floorf(sample_x));
        const int floor_y = static_cast<int>(::floorf(sample_y));
        const int floor_z = static_cast<int>(::floorf(sample_z));
        const float local_x = sample_x - static_cast<float>(floor_x);
        const float local_y = sample_y - static_cast<float>(floor_y);
        const float local_z = sample_z - static_cast<float>(floor_z);
        result += sample(noise, floor_x, floor_y, floor_z,
                         local_x, local_y, local_z) * read_float(noise + 0x11c);
    }
    return result;
}

__attribute__((target("avx2")))
float optimized_multi(const void *self, float x, float y, float z)
{
    const auto *object = static_cast<const unsigned char *>(self);
    const float first = evaluate_noises(
        read_pointer(object), read_pointer(object + 8), x, y, z);
    constexpr float kSecondScale = std::bit_cast<float>(0x3f8251fbU);
    const float second = evaluate_noises(
        read_pointer(object + 24), read_pointer(object + 32),
        x * kSecondScale, y * kSecondScale, z * kSecondScale);
    return (first + second) * read_float(object + 48);
}

extern "C" float checked_multi(const void *self, float x, float y, float z)
{
    if (validation_state.load(std::memory_order_acquire) > 0) {
        return optimized_multi(self, x, y, z);
    }
    const float expected = original(self, x, y, z);
    if (validation_state.load(std::memory_order_relaxed) < 0) {
        return expected;
    }
    const float candidate = optimized_multi(self, x, y, z);
    if (std::bit_cast<std::uint32_t>(expected) !=
        std::bit_cast<std::uint32_t>(candidate)) {
        mismatches.fetch_add(1, std::memory_order_relaxed);
        validation_state.store(-1, std::memory_order_release);
        return expected;
    }
    if (validations.fetch_add(1, std::memory_order_relaxed) + 1 >= kValidationCalls) {
        int validating = 0;
        validation_state.compare_exchange_strong(
            validating, 1, std::memory_order_release, std::memory_order_relaxed);
    }
    return expected;
}

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + kMultiOctaveSample;
    const auto begin = target & ~(page_size - 1);
    const auto end = (target + kOriginal.size() + page_size - 1) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), end - begin};
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
    const auto resume = base + kMultiOctaveSample + kOriginal.size();
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
    original = reinterpret_cast<MultiOctaveSample>(trampoline);
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    if (!__builtin_cpu_supports("avx2")) {
        throw std::runtime_error("AVX2 interpolation requires an AVX2-capable CPU");
    }
    base = native::executable_base();
    auto *target = reinterpret_cast<unsigned char *>(base + kMultiOctaveSample);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS octave evaluator does not match the pinned build");
    }
    validation_state.store(0, std::memory_order_relaxed);
    validations.store(0, std::memory_order_relaxed);
    mismatches.store(0, std::memory_order_relaxed);
    create_trampoline();
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        remove();
        throw std::runtime_error(std::strerror(errno));
    }
    std::array<unsigned char, 14> patch{0x48, 0xb8};
    const auto helper = reinterpret_cast<std::uintptr_t>(&checked_multi);
    std::memcpy(patch.data() + 2, &helper, sizeof(helper));
    patch[10] = 0xff;
    patch[11] = 0xe0;
    patch[12] = 0x90;
    patch[13] = 0x90;
    std::memcpy(target, patch.data(), patch.size());
    __builtin___clear_cache(reinterpret_cast<char *>(target),
                            reinterpret_cast<char *>(target + patch.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        __builtin___clear_cache(reinterpret_cast<char *>(target),
                                reinterpret_cast<char *>(target + kOriginal.size()));
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
    constexpr std::size_t trampoline_size = kOriginal.size() + 13;
    if (installed) {
        auto *target = reinterpret_cast<unsigned char *>(base + kMultiOctaveSample);
        const auto [page, size] = target_pages();
        if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            std::memcpy(target, kOriginal.data(), kOriginal.size());
            __builtin___clear_cache(reinterpret_cast<char *>(target),
                                    reinterpret_cast<char *>(target + kOriginal.size()));
            mprotect(page, size, PROT_READ | PROT_EXEC);
        }
        installed = false;
    }
    if (trampoline != nullptr) {
        munmap(trampoline, trampoline_size);
        trampoline = nullptr;
        original = nullptr;
    }
}

std::size_t mismatch_count() noexcept
{
    return mismatches.load(std::memory_order_relaxed);
}

}  // namespace chunklet::noise::interpolation
