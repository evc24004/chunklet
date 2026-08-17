#include "noise/distance/optimizer.h"

#include "native/layout.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
extern const unsigned char _binary_distance_table_bin_start[]
    __attribute__((visibility("hidden")));
extern const unsigned char _binary_distance_table_bin_end[]
    __attribute__((visibility("hidden")));
}

namespace chunklet::noise::distance {
namespace {
constexpr std::size_t kTableSize = 0xd800;
constexpr std::uintptr_t kInitializer = 0xc9689e0;
constexpr std::uintptr_t kAllocator = 0xe850c08;
constexpr std::uintptr_t kTablePointer = 0xea168a0;
constexpr std::array<unsigned char, 14> kOriginal{
    0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x68,
    0x48, 0x8b, 0x05, 0x1a, 0x82, 0xee, 0x01};

using Allocate = void *(*)(void *, std::size_t);
std::uintptr_t base;
bool installed;

extern "C" __attribute__((noinline)) void install_table()
{
    auto *allocator = reinterpret_cast<void *>(base + kAllocator);
    void **vtable;
    std::memcpy(&vtable, allocator, sizeof(vtable));
    Allocate allocate;
    std::memcpy(&allocate, vtable + 2, sizeof(allocate));
    void *table = allocate(allocator, kTableSize);
    if (table == nullptr) {
        throw std::bad_alloc();
    }
    std::memcpy(table, _binary_distance_table_bin_start, kTableSize);
    std::memcpy(reinterpret_cast<void *>(base + kTablePointer),
                &table, sizeof(table));
}

std::pair<void *, std::size_t> target_page()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto begin = (base + kInitializer) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), page_size};
}

void patch()
{
    auto *target = reinterpret_cast<unsigned char *>(base + kInitializer);
    std::array<unsigned char, kOriginal.size()> code{};
    code.fill(0x90);
    code[0] = 0x48;
    code[1] = 0xb8;
    const auto helper = reinterpret_cast<std::uintptr_t>(&install_table);
    std::memcpy(code.data() + 2, &helper, sizeof(helper));
    code[10] = 0xff;
    code[11] = 0xe0;
    std::memcpy(target, code.data(), code.size());
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    if (static_cast<std::size_t>(_binary_distance_table_bin_end -
                                 _binary_distance_table_bin_start) != kTableSize) {
        throw std::runtime_error("Embedded distance table has an invalid size");
    }
    base = native::executable_base();
    const auto *target = reinterpret_cast<const unsigned char *>(base + kInitializer);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error(
            "BDS distance-table initializer does not match the pinned build");
    }
    const auto [page, size] = target_page();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        throw std::runtime_error(std::strerror(errno));
    }
    patch();
    __builtin___clear_cache(reinterpret_cast<char *>(base + kInitializer),
                            reinterpret_cast<char *>(base + kInitializer + kOriginal.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        std::memcpy(reinterpret_cast<void *>(base + kInitializer),
                    kOriginal.data(), kOriginal.size());
        __builtin___clear_cache(
            reinterpret_cast<char *>(base + kInitializer),
            reinterpret_cast<char *>(base + kInitializer + kOriginal.size()));
        mprotect(page, size, PROT_READ | PROT_EXEC);
        throw std::runtime_error(std::strerror(failure));
    }
    installed = true;
}

void remove() noexcept
{
    if (!installed) {
        return;
    }
    const auto [page, size] = target_page();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        std::memcpy(reinterpret_cast<void *>(base + kInitializer),
                    kOriginal.data(), kOriginal.size());
        __builtin___clear_cache(
            reinterpret_cast<char *>(base + kInitializer),
            reinterpret_cast<char *>(base + kInitializer + kOriginal.size()));
        mprotect(page, size, PROT_READ | PROT_EXEC);
    }
    installed = false;
}

}  // namespace chunklet::noise::distance
