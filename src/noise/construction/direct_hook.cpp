#include "noise/construction/direct.h"

#include "native/layout.h"
#include "noise/construction/direct_runtime.h"

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
constexpr std::uintptr_t kConstructor = 0xc9c8b40;
constexpr std::size_t kTrampolineSize = 30;
constexpr std::array<unsigned char, 17> kOriginal{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
    0x53, 0x48, 0x81, 0xec, 0xe8, 0x03, 0x00, 0x00,
};
std::uintptr_t base;
void *trampoline;
bool installed;

std::pair<void *, std::size_t> target_pages()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto target = base + kConstructor;
    const auto begin = target & ~(page_size - 1);
    const auto end = (target + kOriginal.size() + page_size - 1) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), end - begin};
}

void write_patch(bool optimize)
{
    auto *target = reinterpret_cast<unsigned char *>(base + kConstructor);
    if (!optimize) {
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        return;
    }
    target[0] = 0x48;
    target[1] = 0xb8;
    const auto helper = reinterpret_cast<std::uintptr_t>(&direct_constructor);
    std::memcpy(target + 2, &helper, sizeof(helper));
    target[10] = 0xff;
    target[11] = 0xe0;
    std::memset(target + 12, 0x90, kOriginal.size() - 12);
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
    code[17] = 0x49;
    code[18] = 0xbb;
    const auto continuation = base + kConstructor + kOriginal.size();
    std::memcpy(code + 19, &continuation, sizeof(continuation));
    code[27] = 0x41;
    code[28] = 0xff;
    code[29] = 0xe3;
    if (mprotect(trampoline, kTrampolineSize, PROT_READ | PROT_EXEC) != 0) {
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(errno));
    }
    configure_direct(base, reinterpret_cast<Constructor>(trampoline));
}
}  // namespace

void install_direct()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    auto *target = reinterpret_cast<const unsigned char *>(base + kConstructor);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error(
            "BDS noise constructor prologue does not match the pinned build");
    }
    create_trampoline();
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(errno));
    }
    write_patch(true);
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        write_patch(false);
        mprotect(page, size, PROT_READ | PROT_EXEC);
        munmap(trampoline, kTrampolineSize);
        trampoline = nullptr;
        throw std::runtime_error(std::strerror(errno));
    }
    installed = true;
}

void remove_direct() noexcept
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
    munmap(trampoline, kTrampolineSize);
    trampoline = nullptr;
    configure_direct(0, nullptr);
    installed = false;
}

}  // namespace chunklet::noise::construction
