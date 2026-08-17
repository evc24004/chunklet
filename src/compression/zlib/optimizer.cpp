#include "compression/zlib/optimizer.h"

#include "native/layout.h"

#include <zlib.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::compression::zlib {
namespace {
constexpr std::size_t kPatchSize = 14;
constexpr std::uintptr_t kDeflateInit = 0x427de40;
constexpr std::uintptr_t kDeflateInit2 = 0x427de70;
constexpr std::uintptr_t kDeflateEnd = 0x427e180;
constexpr std::uintptr_t kDeflate = 0x427ef70;
constexpr std::array<unsigned char, kPatchSize> kInitOriginal{
    0x50, 0x89, 0xc8, 0x49, 0x89, 0xd2, 0xba,
    0x08, 0x00, 0x00, 0x00, 0xb9, 0x0f, 0x00};
constexpr std::array<unsigned char, kPatchSize> kInit2Original{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
constexpr std::array<unsigned char, kPatchSize> kEndOriginal{
    0xb8, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x85,
    0xff, 0x0f, 0x84, 0xf5, 0x00, 0x00, 0x00};
constexpr std::array<unsigned char, kPatchSize> kDeflateOriginal{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};

struct Target {
    std::uintptr_t offset;
    const std::array<unsigned char, kPatchSize> *original;
    std::uintptr_t replacement;
};

std::uintptr_t base;
std::size_t patched;

extern "C" std::int32_t fast_deflate_init(
    z_stream *stream, std::int32_t level, const char *, std::int32_t stream_size)
{
    if (stream_size != static_cast<std::int32_t>(sizeof(z_stream))) {
        return Z_VERSION_ERROR;
    }
    return deflateInit(stream, level);
}

extern "C" std::int32_t fast_deflate_init2(
    z_stream *stream, std::int32_t level, std::int32_t method,
    std::int32_t window_bits, std::int32_t memory_level, std::int32_t strategy,
    const char *, std::int32_t stream_size)
{
    if (stream_size != static_cast<std::int32_t>(sizeof(z_stream))) {
        return Z_VERSION_ERROR;
    }
    return deflateInit2(
        stream, level, method, window_bits, memory_level, strategy);
}

extern "C" std::int32_t fast_deflate_end(z_stream *stream)
{
    return deflateEnd(stream);
}

extern "C" std::int32_t fast_deflate(z_stream *stream, std::int32_t flush)
{
    return deflate(stream, flush);
}

std::array<Target, 4> targets()
{
    return {{{kDeflateInit, &kInitOriginal,
              reinterpret_cast<std::uintptr_t>(&fast_deflate_init)},
             {kDeflateInit2, &kInit2Original,
              reinterpret_cast<std::uintptr_t>(&fast_deflate_init2)},
             {kDeflateEnd, &kEndOriginal,
              reinterpret_cast<std::uintptr_t>(&fast_deflate_end)},
             {kDeflate, &kDeflateOriginal,
              reinterpret_cast<std::uintptr_t>(&fast_deflate)}}};
}

bool set_permissions(void *address, int permissions) noexcept
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto page = reinterpret_cast<std::uintptr_t>(address) & ~(page_size - 1);
    return mprotect(reinterpret_cast<void *>(page), page_size, permissions) == 0;
}

void write_target(const Target &target, const unsigned char *bytes)
{
    auto *address = reinterpret_cast<unsigned char *>(base + target.offset);
    if (!set_permissions(address, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        throw std::runtime_error(std::strerror(errno));
    }
    std::memcpy(address, bytes, kPatchSize);
    __builtin___clear_cache(reinterpret_cast<char *>(address),
                            reinterpret_cast<char *>(address + kPatchSize));
    if (!set_permissions(address, PROT_READ | PROT_EXEC)) {
        throw std::runtime_error(std::strerror(errno));
    }
}

std::array<unsigned char, kPatchSize> jump_to(std::uintptr_t replacement)
{
    std::array<unsigned char, kPatchSize> patch{0x48, 0xb8};
    std::memcpy(patch.data() + 2, &replacement, sizeof(replacement));
    patch[10] = 0xff;
    patch[11] = 0xe0;
    patch[12] = 0x90;
    patch[13] = 0x90;
    return patch;
}
}  // namespace

void install()
{
    if (patched != 0) {
        return;
    }
    static_assert(sizeof(z_stream) == 0x70);
    base = native::executable_base();
    const auto entries = targets();
    for (const auto &entry : entries) {
        const auto *address = reinterpret_cast<const unsigned char *>(base + entry.offset);
        if (std::memcmp(address, entry.original->data(), kPatchSize) != 0) {
            throw std::runtime_error("BDS zlib entrypoint does not match the pinned build");
        }
    }
    try {
        for (const auto &entry : entries) {
            const auto patch = jump_to(entry.replacement);
            write_target(entry, patch.data());
            ++patched;
        }
    } catch (...) {
        remove();
        throw;
    }
}

void remove() noexcept
{
    const auto entries = targets();
    while (patched != 0) {
        --patched;
        try {
            write_target(entries[patched], entries[patched].original->data());
        } catch (...) {
        }
    }
}

}  // namespace chunklet::compression::zlib
