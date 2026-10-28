#include "noise/shuffle/optimizer.h"

#include "native/layout.h"

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <utility>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::noise::shuffle {
namespace {
constexpr std::uintptr_t kNextInt = 0x4a83a50;
// Each region is one complete 256-iteration Fisher-Yates loop in the native
// noise constructor: counter = 0x100 counting down, index counting up, the
// random object in a callee-saved register and the bytes on the stack. The
// replacement calls shuffle_values and then leaves every callee-saved register
// exactly as the loop would: random unchanged, counter 0, index 0x100.
// BDS 1.26.60.6 allocates these registers differently per loop.
struct Region {
    std::uintptr_t offset;
    std::uint32_t stack_offset;
    std::string_view random_to_rdi;
    std::string_view finish;
    std::string_view expected;
};
constexpr std::array kRegions{
    // random rbp, counter r12, index rbx
    Region{0xd8138d5, 0x2c4, "4889ef", "4531e4bb00010000",
           "41bc0001000031db0f1f00488b45004889ef4489e6ff501801d848980fb68c1cc40200000fb69404c402000088941cc4020000888c04c402000048ffc349ffcc75c9"},
    // random r15, counter r13, index r14
    Region{0xd813ca6, 0x8c, "4c89ff", "4531ed41be00010000",
           "41bd000100004531f690498b074c89ff4489eeff50184401f04898420fb68c348c0000000fb694048c000000428894348c000000888c048c00000049ffc649ffcd75c7"},
    // random r15, counter r12, index rbx
    Region{0xd814085, 0x19c, "4c89ff", "4531e4bb00010000",
           "41bc0001000031db0f1f00498b074c89ff4489e6ff501801d848980fb68c1c9c0100000fb694049c01000088941c9c010000888c049c01000048ffc349ffcc75ca"},
};
std::uintptr_t base;
bool installed;
std::atomic<int> verification{};

unsigned char hex_digit(char value)
{
    return static_cast<unsigned char>(
        value <= '9' ? value - '0' : value - 'a' + 10);
}

unsigned char expected_byte(std::string_view expected, std::size_t index)
{
    return static_cast<unsigned char>(
        (hex_digit(expected[index * 2]) << 4) |
        hex_digit(expected[index * 2 + 1]));
}

bool matches(const Region &region)
{
    const auto *target = reinterpret_cast<const unsigned char *>(base + region.offset);
    for (std::size_t index = 0; index < region.expected.size() / 2; ++index) {
        if (target[index] != expected_byte(region.expected, index)) {
            return false;
        }
    }
    return true;
}

std::uint32_t next_xoroshiro(
    std::uint64_t &first, std::uint64_t &second, std::uint32_t bound)
{
    for (;;) {
        const auto raw = static_cast<std::uint32_t>(
            std::rotl(first + second, 17) + first);
        second ^= first;
        first = std::rotl(first, 49) ^ second ^ (second << 21);
        second = std::rotl(second, 28);
        const auto product = static_cast<std::uint64_t>(raw) * bound;
        const auto low = static_cast<std::uint32_t>(product);
        if (low >= bound || low >= static_cast<std::uint32_t>(-bound) % bound) {
            return static_cast<std::uint32_t>(product >> 32);
        }
    }
}

bool verify_xoroshiro(void *random)
{
    alignas(std::uint64_t) std::array<unsigned char, 24> reference{};
    auto first = *reinterpret_cast<std::uint64_t *>(
        static_cast<unsigned char *>(random) + 8);
    auto second = *reinterpret_cast<std::uint64_t *>(
        static_cast<unsigned char *>(random) + 0x10);
    std::memcpy(reference.data() + 8, &first, sizeof(first));
    std::memcpy(reference.data() + 0x10, &second, sizeof(second));
    using NextInt = int (*)(void *, int);
    auto next = reinterpret_cast<NextInt>(base + kNextInt);
    for (std::uint32_t index = 0; index < 256; ++index) {
        const auto bound = 256 - index;
        if (next(reference.data(), bound) !=
            static_cast<int>(next_xoroshiro(first, second, bound))) {
            return false;
        }
    }
    return std::memcmp(reference.data() + 8, &first, sizeof(first)) == 0 &&
           std::memcmp(reference.data() + 0x10, &second, sizeof(second)) == 0;
}

void fallback_shuffle(void *random, unsigned char *values, void *target)
{
    using NextInt = int (*)(void *, int);
    auto next = reinterpret_cast<NextInt>(target);
    for (int index = 0; index < 256; ++index) {
        const auto selected = index + next(random, 256 - index);
        std::swap(values[index], values[selected]);
    }
}


std::pair<void *, std::size_t> pages(const Region &region)
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + region.offset;
    const auto begin = target & ~(page_size - 1);
    const auto size = region.expected.size() / 2;
    const auto end = (target + size + page_size - 1) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), end - begin};
}

bool protect(const Region &region, int flags)
{
    const auto [address, size] = pages(region);
    return mprotect(address, size, flags) == 0;
}

void write_region(const Region &region, bool optimize)
{
    auto *target = reinterpret_cast<unsigned char *>(base + region.offset);
    const auto size = region.expected.size() / 2;
    if (!optimize) {
        for (std::size_t index = 0; index < size; ++index) {
            target[index] = expected_byte(region.expected, index);
        }
        return;
    }
    std::memset(target, 0x90, size);
    std::size_t cursor = 0;
    const auto emit = [&](std::string_view hex) {
        for (std::size_t index = 0; index < hex.size() / 2; ++index) {
            target[cursor++] = expected_byte(hex, index);
        }
    };
    emit(region.random_to_rdi);
    const unsigned char argument[] = {0x48, 0x8d, 0xb4, 0x24};  // lea rsi, [rsp + disp32]
    std::memcpy(target + cursor, argument, sizeof(argument));
    cursor += sizeof(argument);
    std::memcpy(target + cursor, &region.stack_offset, sizeof(region.stack_offset));
    cursor += sizeof(region.stack_offset);
    target[cursor++] = 0x48;
    target[cursor++] = 0xb8;
    const auto helper = reinterpret_cast<std::uintptr_t>(&shuffle_values);
    std::memcpy(target + cursor, &helper, sizeof(helper));
    cursor += sizeof(helper);
    target[cursor++] = 0xff;  // call rax
    target[cursor++] = 0xd0;
    emit(region.finish);
}

bool make_writable()
{
    std::size_t changed = 0;
    for (; changed < kRegions.size(); ++changed) {
        if (!protect(kRegions[changed], PROT_READ | PROT_WRITE | PROT_EXEC)) {
            break;
        }
    }
    if (changed == kRegions.size()) {
        return true;
    }
    while (changed != 0) {
        protect(kRegions[--changed], PROT_READ | PROT_EXEC);
    }
    return false;
}

void make_executable() noexcept
{
    for (const auto &region : kRegions) {
        protect(region, PROT_READ | PROT_EXEC);
    }
}
}  // namespace
void shuffle_values(void *random, unsigned char *values)
{
    auto **vtable = *reinterpret_cast<void ***>(random);
    if (reinterpret_cast<std::uintptr_t>(vtable[3]) != base + kNextInt) {
        fallback_shuffle(random, values, vtable[3]);
        return;
    }
    int state = verification.load(std::memory_order_acquire);
    if (state == 0) {
        state = verify_xoroshiro(random) ? 1 : -1;
        int expected = 0;
        verification.compare_exchange_strong(
            expected, state, std::memory_order_release, std::memory_order_acquire);
        state = verification.load(std::memory_order_acquire);
    }
    if (state != 1) {
        fallback_shuffle(random, values, vtable[3]);
        return;
    }
    auto first = *reinterpret_cast<std::uint64_t *>(
        static_cast<unsigned char *>(random) + 8);
    auto second = *reinterpret_cast<std::uint64_t *>(
        static_cast<unsigned char *>(random) + 0x10);
    for (std::uint32_t index = 0; index < 256; ++index) {
        const auto selected = index + next_xoroshiro(first, second, 256 - index);
        std::swap(values[index], values[selected]);
    }
    *reinterpret_cast<std::uint64_t *>(
        static_cast<unsigned char *>(random) + 8) = first;
    *reinterpret_cast<std::uint64_t *>(
        static_cast<unsigned char *>(random) + 0x10) = second;
}

void install()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    for (const auto &region : kRegions) {
        if (!matches(region)) {
            throw std::runtime_error("BDS noise shuffle code does not match the pinned build");
        }
    }
    if (!make_writable()) {
        throw std::runtime_error(std::strerror(errno));
    }
    for (const auto &region : kRegions) {
        write_region(region, true);
    }
    make_executable();
    installed = true;
}

void remove() noexcept
{
    if (!installed || !make_writable()) {
        return;
    }
    for (const auto &region : kRegions) {
        write_region(region, false);
    }
    make_executable();
    installed = false;
}

}  // namespace chunklet::noise::shuffle
