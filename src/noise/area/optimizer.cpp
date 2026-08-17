#include "noise/area/optimizer.h"

#include "native/layout.h"
#include "noise/area/kernel.h"

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

namespace chunklet::noise::area {
namespace {
constexpr std::uintptr_t kReadArea = 0xc9c7cf0;
constexpr std::array<unsigned char, 17> kOriginal{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
    0x53, 0x48, 0x81, 0xec, 0x08, 0x01, 0x00, 0x00};
constexpr std::size_t kValidationCalls = 8;
constexpr std::size_t kValueCount = 5 * 41 * 5;
constexpr std::size_t kTrampolineSize = kOriginal.size() + 12;
using ReadArea = void (*)(void *, float *, const Vec3 *, int, int, int,
                          const Vec3 *, int, int, int, float);

std::uintptr_t base;
void *trampoline;
ReadArea original;
bool installed;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> mismatches{};


bool eligible(const unsigned char *self, int size_x, int size_y, int size_z,
              int step_x, int step_y, int step_z)
{
    return size_x == 5 && size_y == 41 && size_z == 5 &&
           step_x == 1 && step_y == 1 && step_z == 1 &&
           self[0x80c] == 1 && self[0x80d] == 1 && self[0x814] == 1;
}


extern "C" void checked_area(void *self, float *output, const Vec3 *position,
                              int size_x, int size_y, int size_z,
                              const Vec3 *scale, int step_x, int step_y,
                              int step_z, float amplitude)
{
    const auto *bytes = static_cast<const unsigned char *>(self);
    if (!eligible(bytes, size_x, size_y, size_z, step_x, step_y, step_z) ||
        validation_state.load(std::memory_order_acquire) < 0) {
        original(self, output, position, size_x, size_y, size_z, scale,
                 step_x, step_y, step_z, amplitude);
        return;
    }
    if (validation_state.load(std::memory_order_acquire) > 0) {
        evaluate(self, output, *position, size_x, size_y, size_z, *scale,
                 step_x, step_y, step_z, amplitude);
        return;
    }
    alignas(32) std::array<float, kValueCount> candidate_output;
    std::memcpy(candidate_output.data(), output, sizeof(candidate_output));
    original(self, output, position, size_x, size_y, size_z, scale,
             step_x, step_y, step_z, amplitude);
    evaluate(self, candidate_output.data(), *position, size_x, size_y, size_z,
             *scale, step_x, step_y, step_z, amplitude);
    for (std::size_t index = 0; index < kValueCount; ++index) {
        if (std::bit_cast<std::uint32_t>(output[index]) !=
            std::bit_cast<std::uint32_t>(candidate_output[index])) {
            mismatches.fetch_add(1, std::memory_order_relaxed);
            validation_state.store(-1, std::memory_order_release);
            return;
        }
    }
    if (validations.fetch_add(1, std::memory_order_relaxed) + 1 >=
        kValidationCalls) {
        int validating = 0;
        validation_state.compare_exchange_strong(
            validating, 1, std::memory_order_release,
            std::memory_order_relaxed);
    }
}

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + kReadArea;
    const auto begin = target & ~(page_size - 1);
    const auto end = (target + kOriginal.size() + page_size - 1) & ~(page_size - 1);
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
    code[cursor++] = 0x48;
    code[cursor++] = 0xb8;
    const auto resume = base + kReadArea + kOriginal.size();
    std::memcpy(code + cursor, &resume, sizeof(resume));
    cursor += sizeof(resume);
    code[cursor++] = 0xff;
    code[cursor] = 0xe0;
    if (mprotect(trampoline, kTrampolineSize, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(failure));
    }
    original = reinterpret_cast<ReadArea>(trampoline);
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    if (!__builtin_cpu_supports("avx2")) {
        throw std::runtime_error("AVX2 area evaluator requires an AVX2-capable CPU");
    }
    base = native::executable_base();
    auto *target = reinterpret_cast<unsigned char *>(base + kReadArea);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS area evaluator does not match the pinned build");
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
    std::array<unsigned char, 17> patch{0x48, 0xb8};
    const auto helper = reinterpret_cast<std::uintptr_t>(&checked_area);
    std::memcpy(patch.data() + 2, &helper, sizeof(helper));
    patch[10] = 0xff;
    patch[11] = 0xe0;
    std::fill(patch.begin() + 12, patch.end(), 0x90);
    std::memcpy(target, patch.data(), patch.size());
    __builtin___clear_cache(reinterpret_cast<char *>(target),
                            reinterpret_cast<char *>(target + patch.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        __builtin___clear_cache(reinterpret_cast<char *>(target),
                                reinterpret_cast<char *>(target + kOriginal.size()));
        mprotect(page, size, PROT_READ | PROT_EXEC);
        remove();
        throw std::runtime_error(std::strerror(failure));
    }
    installed = true;
}

void remove() noexcept
{
    if (installed) {
        auto *target = reinterpret_cast<unsigned char *>(base + kReadArea);
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


}  // namespace chunklet::noise::area
