#include "noise/construction/reserve.h"

#include "native/layout.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::noise::construction {
namespace {
constexpr std::uintptr_t kConstructorInit = 0xc9c8b56;
constexpr std::uintptr_t kAllocator = 0xe850c08;
constexpr std::size_t kElementSize = 0x124;
constexpr std::array<unsigned char, 14> kOriginal{
    0x0f, 0x57, 0xc0, 0x0f, 0x11, 0x07, 0x48,
    0xc7, 0x47, 0x10, 0x00, 0x00, 0x00, 0x00,
};

std::uintptr_t base;
bool installed;

extern "C" __attribute__((used, noinline)) void reserve_output(
    void *output, const void *amplitudes)
{
    const auto *words = static_cast<const std::uintptr_t *>(amplitudes);
    allocate_output(output, (words[1] - words[0]) / sizeof(float));
}

extern "C" __attribute__((naked)) void reserve_entry()
{
    asm volatile(
        "push %r10\n"
        "push %r9\n"
        "push %r8\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "mov %rcx, %rsi\n"
        "call reserve_output\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %r8\n"
        "pop %r9\n"
        "pop %r10\n"
        "ret\n");
}

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + kConstructorInit;
    const auto begin = target & ~(page_size - 1);
    const auto end = (target + kOriginal.size() + page_size - 1) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), end - begin};
}

void write_patch(bool optimize)
{
    auto *target = reinterpret_cast<unsigned char *>(base + kConstructorInit);
    if (!optimize) {
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        return;
    }
    target[0] = 0x48;
    target[1] = 0xb8;
    const auto helper = reinterpret_cast<std::uintptr_t>(&reserve_entry);
    std::memcpy(target + 2, &helper, sizeof(helper));
    target[10] = 0xff;
    target[11] = 0xd0;
    target[12] = 0x90;
    target[13] = 0x90;
}
}  // namespace
void allocate_output(void *output, std::size_t count)
{
    auto *vector = static_cast<std::uintptr_t *>(output);
    vector[0] = 0;
    vector[1] = 0;
    vector[2] = 0;
    if (count == 0) {
        return;
    }

    auto *allocator = reinterpret_cast<void *>(base + kAllocator);
    auto **vtable = *reinterpret_cast<void ***>(allocator);
    using Allocate = void *(*)(void *, std::size_t);
    auto allocate = reinterpret_cast<Allocate>(vtable[2]);
    auto *storage = static_cast<unsigned char *>(
        allocate(allocator, count * kElementSize));
    if (storage == nullptr) {
        return;
    }
    vector[0] = reinterpret_cast<std::uintptr_t>(storage);
    vector[1] = reinterpret_cast<std::uintptr_t>(storage);
    vector[2] = reinterpret_cast<std::uintptr_t>(storage + count * kElementSize);
}


void install()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    auto *target = reinterpret_cast<const unsigned char *>(base + kConstructorInit);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS noise constructor code does not match the pinned build");
    }
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        throw std::runtime_error(std::strerror(errno));
    }
    write_patch(true);
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        write_patch(false);
        mprotect(page, size, PROT_READ | PROT_EXEC);
        throw std::runtime_error(std::strerror(errno));
    }
    installed = true;
}

void remove() noexcept
{
    if (!installed) {
        return;
    }
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return;
    }
    write_patch(false);
    mprotect(page, size, PROT_READ | PROT_EXEC);
    installed = false;
}

}  // namespace chunklet::noise::construction
