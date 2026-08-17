#include "allocation/transient/object_pool.h"
#include "allocation/transient/object_copy.h"
#include "native/layout.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::allocation::transient {
namespace {
constexpr std::size_t kObjectSize = 0x1b0;
constexpr std::uintptr_t kFirstAllocation = 0x4ec0d0f;
constexpr std::uintptr_t kSecondAllocation = 0x4ec0ed9;
constexpr std::uintptr_t kFirstRelease = 0x4ec0f57;
constexpr std::uintptr_t kSecondRelease = 0x4ec0f87;
constexpr std::uintptr_t kRedundantCopy = 0x4ec0f42;
constexpr std::uintptr_t kInitializationStart = 0x4ec0d64;
constexpr std::uintptr_t kInitializationEnd = 0x4ec0ec0;
constexpr std::size_t kInitializationSize =
    kInitializationEnd - kInitializationStart;
constexpr std::uint64_t kInitializationFingerprint = 0xa14f49199be94693ULL;
constexpr std::uintptr_t kTemplatePair = 0xe860398;
constexpr std::uintptr_t kTemplateRemainder = 0xe860548;
constexpr std::array<unsigned char, 22> kFirstAllocationOriginal{
    0x48, 0x8b, 0x05, 0xf2, 0xfe, 0x98, 0x09,
    0x48, 0x8d, 0x3d, 0xeb, 0xfe, 0x98, 0x09,
    0xbe, 0xb0, 0x01, 0x00, 0x00, 0xff, 0x50, 0x10};
constexpr std::array<unsigned char, 22> kSecondAllocationOriginal{
    0x48, 0x8d, 0x3d, 0x28, 0xfd, 0x98, 0x09,
    0x48, 0x8b, 0x05, 0x21, 0xfd, 0x98, 0x09,
    0xbe, 0xb0, 0x01, 0x00, 0x00, 0xff, 0x50, 0x10};
constexpr std::array<unsigned char, 13> kFirstReleaseOriginal{
    0xbe, 0xb0, 0x01, 0x00, 0x00, 0x4c, 0x89,
    0xf7, 0xe8, 0x1c, 0xb2, 0xde, 0xfe};
constexpr std::array<unsigned char, 13> kSecondReleaseOriginal{
    0xbe, 0xb0, 0x01, 0x00, 0x00, 0x48, 0x89,
    0xdf, 0xe8, 0xec, 0xb1, 0xde, 0xfe};
constexpr std::array<unsigned char, 16> kRedundantCopyOriginal{
    0xba, 0xb0, 0x01, 0x00, 0x00, 0x48, 0x89, 0xdf,
    0x4c, 0x89, 0xf6, 0xe8, 0x0e, 0xb9, 0x35, 0x09};

std::uintptr_t base;
bool installed;
std::array<unsigned char, kInitializationSize> initialization_original;
alignas(32) std::array<unsigned char, kObjectSize> object_template;

extern "C" __attribute__((noinline)) void *first_slot() noexcept
{
    return prepare_thread_object(object_template.data());
}

extern "C" __attribute__((noinline)) void release_slot() noexcept
{
}

template <std::size_t Size>
void verify(std::uintptr_t offset,
            const std::array<unsigned char, Size> &expected)
{
    const auto *target = reinterpret_cast<const unsigned char *>(base + offset);
    if (std::memcmp(target, expected.data(), Size) != 0) {
        throw std::runtime_error("BDS transient object site does not match the pinned build");
    }
}

template <std::size_t Size>
void patch_call(std::uintptr_t offset, const void *helper)
{
    static_assert(Size >= 12);
    auto *target = reinterpret_cast<unsigned char *>(base + offset);
    std::memset(target, 0x90, Size);
    target[0] = 0x48;
    target[1] = 0xb8;
    const auto address = reinterpret_cast<std::uintptr_t>(helper);
    std::memcpy(target + 2, &address, sizeof(address));
    target[10] = 0xff;
    target[11] = 0xd0;
}

void patch_reuse()
{
    auto *target = reinterpret_cast<unsigned char *>(base + kSecondAllocation);
    std::memset(target, 0x90, kSecondAllocationOriginal.size());
    const std::array<unsigned char, 3> move{0x4c, 0x89, 0xf0};
    std::memcpy(target, move.data(), move.size());
}

template <std::size_t Size>
void restore(std::uintptr_t offset,
             const std::array<unsigned char, Size> &original)
{
    std::memcpy(reinterpret_cast<void *>(base + offset), original.data(), Size);
}

std::uint64_t fingerprint(const unsigned char *bytes, std::size_t size)
{
    std::uint64_t value = 0xcbf29ce484222325ULL;
    for (std::size_t index = 0; index < size; ++index) {
        value ^= bytes[index];
        value *= 0x100000001b3ULL;
    }
    return value;
}

void prepare_template()
{
    const auto pair = base + kTemplatePair;
    std::uint64_t first;
    std::uint64_t second;
    std::uint64_t remainder;
    std::memcpy(&first, reinterpret_cast<const void *>(pair), sizeof(first));
    std::memcpy(&second, reinterpret_cast<const void *>(pair + 8), sizeof(second));
    std::memcpy(&remainder, reinterpret_cast<const void *>(
                    base + kTemplateRemainder), sizeof(remainder));
    const auto put = [](std::size_t offset, std::uint64_t value) {
        std::memcpy(object_template.data() + offset, &value, sizeof(value));
    };
    for (const auto offset :
         {0x0, 0x10, 0x68, 0x78, 0xd8, 0xe8, 0x140, 0x150}) {
        put(offset, first);
    }
    for (const auto offset :
         {0x8, 0x18, 0x28, 0x70, 0x80, 0x90, 0xe0, 0xf0,
          0x100, 0x148, 0x158, 0x168}) {
        put(offset, second);
    }
    for (const auto offset : {0x20, 0x88, 0xf8, 0x160}) {
        put(offset, remainder);
    }
    for (const auto offset : {0x30, 0x98, 0x108, 0x170}) {
        put(offset, first);
        put(offset + 8, second);
    }
    put(0x50, static_cast<std::uint64_t>(-10'000));
    put(0x58, static_cast<std::uint64_t>(-1'599));
    put(0xb8, static_cast<std::uint64_t>(-10'000));
    put(0xc0, static_cast<std::uint64_t>(-1'599));
    put(0x128, 1'599);
    put(0x130, 10'000);
    put(0x190, 1'599);
    put(0x198, 10'000);
}

void skip_initialization()
{
    auto *target =
        reinterpret_cast<unsigned char *>(base + kInitializationStart);
    std::memset(target, 0x90, kInitializationSize);
    target[0] = 0x48;
    target[1] = 0xb8;
    const auto resume = base + kInitializationEnd;
    std::memcpy(target + 2, &resume, sizeof(resume));
    target[10] = 0xff;
    target[11] = 0xe0;
}

std::pair<void *, std::size_t> target_page()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto begin = (base + kFirstAllocation) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), page_size};
}

void restore_all()
{
    restore(kFirstAllocation, kFirstAllocationOriginal);
    restore(kSecondAllocation, kSecondAllocationOriginal);
    restore(kFirstRelease, kFirstReleaseOriginal);
    restore(kSecondRelease, kSecondReleaseOriginal);
    restore(kRedundantCopy, kRedundantCopyOriginal);
    std::memcpy(reinterpret_cast<void *>(base + kInitializationStart),
                initialization_original.data(), kInitializationSize);
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    verify(kFirstAllocation, kFirstAllocationOriginal);
    verify(kSecondAllocation, kSecondAllocationOriginal);
    verify(kFirstRelease, kFirstReleaseOriginal);
    verify(kSecondRelease, kSecondReleaseOriginal);
    verify(kRedundantCopy, kRedundantCopyOriginal);
    const auto *initialization = reinterpret_cast<const unsigned char *>(
        base + kInitializationStart);
    if (fingerprint(initialization, kInitializationSize) !=
        kInitializationFingerprint) {
        throw std::runtime_error(
            "BDS transient object initialization does not match the pinned build");
    }
    std::memcpy(initialization_original.data(), initialization,
                kInitializationSize);
    prepare_template();
    const auto [page, size] = target_page();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        throw std::runtime_error(std::strerror(errno));
    }
    patch_call<kFirstAllocationOriginal.size()>(
        kFirstAllocation, reinterpret_cast<const void *>(&first_slot));
    patch_reuse();
    patch_call<kFirstReleaseOriginal.size()>(
        kFirstRelease, reinterpret_cast<const void *>(&release_slot));
    patch_call<kSecondReleaseOriginal.size()>(
        kSecondRelease, reinterpret_cast<const void *>(&release_slot));
    skip_initialization();
    std::memset(reinterpret_cast<void *>(base + kRedundantCopy),
                0x90, kRedundantCopyOriginal.size());
    __builtin___clear_cache(
        reinterpret_cast<char *>(base + kFirstAllocation),
        reinterpret_cast<char *>(
            base + kSecondRelease + kSecondReleaseOriginal.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        restore_all();
        __builtin___clear_cache(
            reinterpret_cast<char *>(base + kFirstAllocation),
            reinterpret_cast<char *>(base + kSecondRelease + kSecondReleaseOriginal.size()));
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
        restore_all();
        __builtin___clear_cache(
            reinterpret_cast<char *>(base + kFirstAllocation),
            reinterpret_cast<char *>(base + kSecondRelease + kSecondReleaseOriginal.size()));
        mprotect(page, size, PROT_READ | PROT_EXEC);
    }
    installed = false;
}
}  // namespace chunklet::allocation::transient
