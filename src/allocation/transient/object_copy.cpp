#include "allocation/transient/object_copy.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <immintrin.h>

namespace chunklet::allocation::transient {
namespace {
constexpr std::size_t kObjectSize = 0x1b0;
constexpr std::size_t kSlotCount = 256;

struct alignas(64) ThreadSlot {
    std::atomic<const void *> owner{};
    alignas(32) std::array<unsigned char, kObjectSize> bytes;
};

std::array<ThreadSlot, kSlotCount> slots;

ThreadSlot &slot_for_thread() noexcept
{
    const void *identity;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(identity));
    const auto key = reinterpret_cast<std::uintptr_t>(identity) *
                     0x9e3779b97f4a7c15ULL;
    std::size_t index = key >> 56U;
    for (std::size_t probe = 0; probe < kSlotCount; ++probe) {
        auto &slot = slots[index];
        const void *owner = slot.owner.load(std::memory_order_acquire);
        if (owner == identity ||
            (owner == nullptr && slot.owner.compare_exchange_strong(
                                     owner, identity,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire))) {
            return slot;
        }
        index = (index + 1) & (kSlotCount - 1);
    }
    std::abort();
}
}  // namespace

__attribute__((target("avx2")))
unsigned char *prepare_thread_object(const unsigned char *source) noexcept
{
    auto &destination = slot_for_thread().bytes;
    for (std::size_t offset = 0; offset < 416; offset += 32) {
        const auto value = _mm256_load_si256(
            reinterpret_cast<const __m256i *>(source + offset));
        _mm256_store_si256(
            reinterpret_cast<__m256i *>(destination.data() + offset), value);
    }
    const auto tail = _mm_load_si128(
        reinterpret_cast<const __m128i *>(source + 416));
    _mm_store_si128(
        reinterpret_cast<__m128i *>(destination.data() + 416), tail);
    return destination.data();
}

}  // namespace chunklet::allocation::transient
