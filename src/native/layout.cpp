#include "native/layout.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <elf.h>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace chunklet::native {
namespace {

std::string executable_path()
{
    std::array<char, 4096> path{};
    const auto size = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (size < 0) {
        throw std::runtime_error("cannot read /proc/self/exe");
    }
    return {path.data(), static_cast<std::size_t>(size)};
}

template <typename T>
T read_object(std::ifstream &input, std::streamoff offset)
{
    T value{};
    input.seekg(offset);
    input.read(reinterpret_cast<char *>(&value), sizeof(value));
    if (!input) {
        throw std::runtime_error("cannot read BDS ELF metadata");
    }
    return value;
}

std::size_t align4(std::size_t value)
{
    return (value + 3U) & ~std::size_t{3U};
}

void check_name(std::uintptr_t base, std::uintptr_t slot, std::string_view expected)
{
    const auto *name = *reinterpret_cast<const char *const *>(base + slot);
    if (name == nullptr || name != expected) {
        throw std::runtime_error("BDS RTTI layout does not match " + std::string(expected));
    }
}

void check_slot(void **vtable, int slot, std::uintptr_t expected,
                std::uintptr_t base, std::string_view name)
{
    if (reinterpret_cast<std::uintptr_t>(vtable[slot]) != base + expected) {
        throw std::runtime_error("BDS vtable slot does not match " + std::string(name));
    }
}

}  // namespace

std::uintptr_t executable_base()
{
    const auto path = executable_path();
    std::ifstream maps("/proc/self/maps");
    if (!maps) {
        throw std::runtime_error("cannot open /proc/self/maps");
    }
    for (std::string line; std::getline(maps, line);) {
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        std::uintptr_t offset = 0;
        char permissions[5]{};
        char device[64]{};
        unsigned long inode = 0;
        char mapped_path[4096]{};
        const int fields = std::sscanf(line.c_str(), "%lx-%lx %4s %lx %63s %lu %4095s",
                                       &begin, &end, permissions, &offset, device, &inode,
                                       mapped_path);
        if (fields == 7 && path == mapped_path) {
            return begin - offset;
        }
    }
    throw std::runtime_error("BDS executable mapping is not available");
}

std::string executable_build_id()
{
    std::ifstream input(executable_path(), std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open the BDS executable");
    }
    const auto header = read_object<Elf64_Ehdr>(input, 0);
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS64) {
        throw std::runtime_error("BDS executable is not a 64-bit ELF file");
    }

    for (std::uint16_t index = 0; index < header.e_phnum; ++index) {
        const auto offset = static_cast<std::streamoff>(header.e_phoff) +
                            static_cast<std::streamoff>(index) * header.e_phentsize;
        const auto program = read_object<Elf64_Phdr>(input, offset);
        if (program.p_type != PT_NOTE || program.p_filesz > 1024 * 1024) {
            continue;
        }
        std::vector<unsigned char> notes(static_cast<std::size_t>(program.p_filesz));
        input.seekg(static_cast<std::streamoff>(program.p_offset));
        input.read(reinterpret_cast<char *>(notes.data()),
                   static_cast<std::streamsize>(notes.size()));
        if (!input) {
            throw std::runtime_error("cannot read BDS ELF notes");
        }
        for (std::size_t cursor = 0; cursor + sizeof(Elf64_Nhdr) <= notes.size();) {
            Elf64_Nhdr note{};
            std::memcpy(&note, notes.data() + cursor, sizeof(note));
            cursor += sizeof(note);
            const auto name_start = cursor;
            const auto desc_start = cursor + align4(note.n_namesz);
            const auto next = desc_start + align4(note.n_descsz);
            if (next > notes.size()) {
                break;
            }
            const bool is_build_id = note.n_type == NT_GNU_BUILD_ID &&
                                     note.n_namesz >= 3 &&
                                     std::memcmp(notes.data() + name_start, "GNU", 3) == 0;
            if (is_build_id) {
                static constexpr char hex[] = "0123456789abcdef";
                std::string result;
                result.reserve(static_cast<std::size_t>(note.n_descsz) * 2);
                for (std::size_t i = 0; i < note.n_descsz; ++i) {
                    const auto byte = notes[desc_start + i];
                    result.push_back(hex[byte >> 4]);
                    result.push_back(hex[byte & 0x0f]);
                }
                return result;
            }
            cursor = next;
        }
    }
    throw std::runtime_error("BDS GNU build ID is not available");
}

void verify_layout()
{
    const auto build_id = executable_build_id();
    if (build_id != kSupportedBuildId) {
        throw std::runtime_error("unsupported BDS GNU build ID: " + build_id);
    }
    const auto base = executable_base();
    check_name(base, kOverworldTypeNameSlot, "18OverworldDimension");
    check_name(base, kBlockSourceTypeNameSlot, "11BlockSource");
    check_name(base, kMainChunkSourceTypeNameSlot, "15MainChunkSource");
    check_name(base, kNetworkChunkSourceTypeNameSlot, "18NetworkChunkSource");
    check_name(base, kWorldLimitChunkSourceTypeNameSlot, "21WorldLimitChunkSource");

    auto **dimension = reinterpret_cast<void **>(base + kOverworldVtable);
    auto **block_source = reinterpret_cast<void **>(base + kBlockSourceVtable);
    check_slot(dimension, kDimensionBlockSourceSlot, kDimensionBlockSourceTarget,
               base, "Dimension::getBlockSourceFromMainChunkSource");
    check_slot(block_source, kBlockSourceChunkSourceSlot, kBlockSourceChunkSourceTarget,
               base, "BlockSource::getChunkSource");

    for (const auto [vtable_address, load_target] :
         std::array{std::pair{kMainChunkSourceVtable, kMainGetOrLoadTarget},
                    std::pair{kNetworkChunkSourceVtable, kNetworkGetOrLoadTarget},
                    std::pair{kWorldLimitChunkSourceVtable, kMainGetOrLoadTarget}}) {
        auto **vtable = reinterpret_cast<void **>(base + vtable_address);
        check_slot(vtable, kGetOrLoadSlot, load_target, base, "ChunkSource::getOrLoadChunk");
        check_slot(vtable, kSaveLiveChunkSlot, kSaveLiveChunkTarget, base,
                   "ChunkSource::saveLiveChunk");
        check_slot(vtable, kFlushThreadBatchSlot, kFlushThreadBatchTarget, base,
                   "ChunkSource::flushThreadBatch");
    }
}

int chunk_state(const void *chunk)
{
    if (chunk == nullptr) {
        return -1;
    }
    return *reinterpret_cast<const unsigned char *>(
        static_cast<const char *>(chunk) + kChunkStateOffset);
}

}  // namespace chunklet::native
