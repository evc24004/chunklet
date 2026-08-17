#include "cache/ownership/optimizer.h"

#include "native/layout.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::cache::ownership {
namespace {
constexpr std::uintptr_t kResolve = 0xc0e1180;
constexpr std::array<unsigned char, 14> kOriginal{
    0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53,
    0x48, 0x83, 0xec, 0x18, 0x48, 0x89, 0xf2};
constexpr std::size_t kTrampolineSize = kOriginal.size() + 14;

std::uintptr_t base;
void *trampoline;
bool installed;

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto begin = (base + kResolve) & ~(page_size - 1);
    const auto end = (base + kResolve + kOriginal.size() + page_size - 1) &
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
    const auto resume = base + kResolve + kOriginal.size();
    std::memcpy(code + cursor, &resume, sizeof(resume));
    if (mprotect(trampoline, kTrampolineSize, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(failure));
    }
}
}  // namespace

extern "C" {
__attribute__((used, retain, visibility("hidden")))
void *chunklet_ownership_fallback;
}

extern "C" __attribute__((naked, visibility("hidden"))) void
chunklet_ownership_entry()
{
    __asm__ volatile(
        "mov 0x90(%rdi), %rax\n"
        "cmp (%rsi), %rax\n"
        "jne 1f\n"
        "mov 0xa0(%rdi), %rax\n"
        "test %rax, %rax\n"
        "je 1f\n"
        "mov 0x8(%rax), %rax\n"
        "cmp $-1, %rax\n"
        "je 1f\n"
        "mov 0xa8(%rdi), %rax\n"
        "ret\n"
        "1: jmp *chunklet_ownership_fallback(%rip)\n");
}

namespace {
void write_patch(bool optimize)
{
    auto *target = reinterpret_cast<unsigned char *>(base + kResolve);
    if (!optimize) {
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        return;
    }
    std::array<unsigned char, kOriginal.size()> patch{};
    patch.fill(0x90);
    patch[0] = 0xff;
    patch[1] = 0x25;
    std::memset(patch.data() + 2, 0, sizeof(std::uint32_t));
    const auto entry =
        reinterpret_cast<std::uintptr_t>(&chunklet_ownership_entry);
    std::memcpy(patch.data() + 6, &entry, sizeof(entry));
    std::memcpy(target, patch.data(), patch.size());
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    const auto *target =
        reinterpret_cast<const unsigned char *>(base + kResolve);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS ownership resolver does not match the pinned build");
    }
    create_trampoline();
    chunklet_ownership_fallback = trampoline;
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        remove();
        throw std::runtime_error(std::strerror(errno));
    }
    write_patch(true);
    __builtin___clear_cache(
        reinterpret_cast<char *>(base + kResolve),
        reinterpret_cast<char *>(base + kResolve + kOriginal.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        write_patch(false);
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
                reinterpret_cast<char *>(base + kResolve),
                reinterpret_cast<char *>(base + kResolve + kOriginal.size()));
            mprotect(page, size, PROT_READ | PROT_EXEC);
        }
        installed = false;
    }
    if (trampoline != nullptr) {
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        chunklet_ownership_fallback = nullptr;
    }
}

}  // namespace chunklet::cache::ownership
