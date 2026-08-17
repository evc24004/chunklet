#include "spatial/containment/optimizer.h"

#include "native/layout.h"
#include "spatial/containment/kernel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::spatial::containment {
namespace {
constexpr std::uintptr_t kEvaluate = 0xc969a20;
constexpr std::uintptr_t kDenseTable = 0xea16890;
constexpr std::uintptr_t kLocalTable = 0xea168a0;
constexpr std::array<unsigned char, 16> kOriginal{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41,
    0x54, 0x53, 0x48, 0x83, 0xec, 0x18, 0x89, 0xd3};
constexpr std::size_t kValidationCalls = 64;
constexpr std::size_t kTrampolineSize = kOriginal.size() + 12;
using Evaluate = double (*)(const void *, std::uint64_t, int);

std::uintptr_t base;
void *trampoline;
Evaluate original;
bool installed;
std::once_flag tables_once;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> mismatches{};

void prepare_tables()
{
    const auto dense = *reinterpret_cast<const float *const *>(base + kDenseTable);
    const auto local = *reinterpret_cast<const float *const *>(base + kLocalTable);
    if (dense == nullptr || local == nullptr) {
        throw std::runtime_error("BDS containment tables were not initialized");
    }
    compact_tables(dense, local);
}

extern "C" double checked_containment(const void *source,
                                      std::uint64_t packed_xy, int z)
{
    const int state = validation_state.load(std::memory_order_acquire);
    if (state < 0) {
        return original(source, packed_xy, z);
    }
    if (state > 0) {
        return evaluate(source, packed_xy, z);
    }
    const double expected = original(source, packed_xy, z);
    std::call_once(tables_once, prepare_tables);
    const double actual = evaluate(source, packed_xy, z);
    if (std::bit_cast<std::uint64_t>(expected) !=
        std::bit_cast<std::uint64_t>(actual)) {
        mismatches.fetch_add(1, std::memory_order_relaxed);
        validation_state.store(-1, std::memory_order_release);
        return expected;
    }
    if (validations.fetch_add(1, std::memory_order_relaxed) + 1 >=
        kValidationCalls) {
        int validating = 0;
        validation_state.compare_exchange_strong(
            validating, 1, std::memory_order_release,
            std::memory_order_relaxed);
    }
    return expected;
}

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + kEvaluate;
    const auto begin = target & ~(page_size - 1);
    const auto end = (target + kOriginal.size() + page_size - 1) &
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
    code[cursor++] = 0x48;
    code[cursor++] = 0xb8;
    const auto resume = base + kEvaluate + kOriginal.size();
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
    original = reinterpret_cast<Evaluate>(trampoline);
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    if (!__builtin_cpu_supports("avx2")) {
        throw std::runtime_error(
            "Batched containment evaluator requires an AVX2-capable CPU");
    }
    base = native::executable_base();
    auto *target = reinterpret_cast<unsigned char *>(base + kEvaluate);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error(
            "BDS containment evaluator does not match the pinned build");
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
    std::array<unsigned char, 16> patch{0x48, 0xb8};
    const auto helper = reinterpret_cast<std::uintptr_t>(&checked_containment);
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
        auto *target = reinterpret_cast<unsigned char *>(base + kEvaluate);
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


}  // namespace chunklet::spatial::containment
