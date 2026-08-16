#include "native/chunk_source.h"

#include "native/layout.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace chunklet::native {
namespace {

const char *rtti_name(const void *object)
{
    if (object == nullptr) {
        return nullptr;
    }
    auto *const *vtable = *reinterpret_cast<const void *const *const *>(object);
    if (vtable == nullptr || vtable[-1] == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<const char *const *>(
        static_cast<const char *>(vtable[-1]) + 8);
}

bool known_dimension(const void *dimension, std::uintptr_t base)
{
    if (*reinterpret_cast<const std::uintptr_t *>(dimension) == base + kOverworldVtable) {
        return true;
    }
    const auto *name = rtti_name(dimension);
    return name != nullptr &&
           (std::strcmp(name, "15NetherDimension") == 0 ||
            std::strcmp(name, "15TheEndDimension") == 0);
}

bool known_chunk_source(const void *source, std::uintptr_t base)
{
    const auto vtable = *reinterpret_cast<const std::uintptr_t *>(source);
    return vtable == base + kMainChunkSourceVtable ||
           vtable == base + kNetworkChunkSourceVtable ||
           vtable == base + kWorldLimitChunkSourceVtable;
}

}  // namespace

ChunkSource ChunkSource::resolve(void *endstone_dimension)
{
    if (endstone_dimension == nullptr) {
        throw std::runtime_error("Endstone dimension is null");
    }
    const auto base = executable_base();
    auto *dimension = *reinterpret_cast<void **>(
        static_cast<char *>(endstone_dimension) + kEndstoneDimensionHandleOffset);
    if (dimension == nullptr || !known_dimension(dimension, base)) {
        throw std::runtime_error("Endstone dimension handle does not match the supported BDS layout");
    }

    auto **dimension_vtable = *reinterpret_cast<void ***>(dimension);
    using GetBlockSource = void *(*)(void *);
    auto *block_source = reinterpret_cast<GetBlockSource>(
        dimension_vtable[kDimensionBlockSourceSlot])(dimension);
    if (block_source == nullptr ||
        *reinterpret_cast<std::uintptr_t *>(block_source) != base + kBlockSourceVtable) {
        throw std::runtime_error("BDS BlockSource does not match the supported layout");
    }

    auto **block_vtable = *reinterpret_cast<void ***>(block_source);
    using GetChunkSource = void *(*)(void *);
    auto *chunk_source = reinterpret_cast<GetChunkSource>(
        block_vtable[kBlockSourceChunkSourceSlot])(block_source);
    if (chunk_source == nullptr || !known_chunk_source(chunk_source, base)) {
        const auto *name = rtti_name(chunk_source);
        throw std::runtime_error("BDS ChunkSource does not match the supported layout: " +
                                 std::string(name == nullptr ? "<null>" : name));
    }
    return ChunkSource(chunk_source);
}

std::shared_ptr<void> ChunkSource::request(render::ChunkPosition position) const
{
    if (!valid()) {
        throw std::logic_error("BDS ChunkSource is not resolved");
    }
    auto **vtable = *reinterpret_cast<void ***>(handle_);
    using GetOrLoad = void *(*)(void *, void *, const render::ChunkPosition *, int, bool);
    auto *get_or_load = reinterpret_cast<GetOrLoad>(vtable[kGetOrLoadSlot]);
    std::shared_ptr<void> chunk;
    get_or_load(&chunk, handle_, &position, kDeferredLoadMode, false);
    return chunk;
}


bool ChunkSource::save(void *chunk) const
{
    auto **vtable = *reinterpret_cast<void ***>(handle_);
    using SaveLiveChunk = bool (*)(void *, void *);
    return reinterpret_cast<SaveLiveChunk>(vtable[kSaveLiveChunkSlot])(handle_, chunk);
}

void ChunkSource::flush() const
{
    auto **vtable = *reinterpret_cast<void ***>(handle_);
    using FlushThreadBatch = void (*)(void *);
    reinterpret_cast<FlushThreadBatch>(vtable[kFlushThreadBatchSlot])(handle_);
}

}  // namespace chunklet::native
