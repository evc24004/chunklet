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
void *db_storage(void *source, std::uintptr_t base)
{
    auto *storage = *reinterpret_cast<void **>(
        static_cast<char *>(source) + 0x30);
    if (storage == nullptr ||
        *reinterpret_cast<std::uintptr_t *>(storage) != base + kDbChunkStorageVtable) {
        throw std::runtime_error("BDS ChunkSource storage does not match DBChunkStorage");
    }
    return storage;
}

void *current_write_batch(std::uintptr_t base)
{
    auto *thread = static_cast<char *>(__builtin_thread_pointer());
    if (*(thread - kWriteBatchInitializedTlsOffset) == 0) {
        throw std::runtime_error("BDS thread-local write batch was not initialized");
    }
    auto *batch = thread - kWriteBatchTlsOffset;
    if (*reinterpret_cast<std::uintptr_t *>(batch) !=
        base + kLevelStorageWriteBatchVtable) {
        throw std::runtime_error("BDS thread-local write batch does not match the supported layout");
    }
    return batch;
}
// The pinned BDS batch ABI consumes libc++'s 24-byte inline string layout.
struct BdsSmallString {
    alignas(void *) unsigned char bytes[24]{};
};
static_assert(sizeof(BdsSmallString) == 24);

BdsSmallString small_string(const void *data, std::size_t size)
{
    if (size > 22) {
        throw std::length_error("BDS inline string capacity exceeded");
    }
    BdsSmallString value;
    value.bytes[0] = static_cast<unsigned char>(size << 1);
    std::memcpy(value.bytes + 1, data, size);
    return value;
}

void write_finalized_record(
    void *batch, const void *chunk, const void *dimension, std::uintptr_t base)
{
    unsigned char key_bytes[13]{};
    std::memcpy(key_bytes, static_cast<const char *>(chunk) + 0x50, 8);
    std::size_t key_size = 8;
    if (*reinterpret_cast<const std::uintptr_t *>(dimension) !=
        base + kOverworldVtable) {
        const auto *name = rtti_name(dimension);
        const std::int32_t dimension_id =
            name != nullptr && std::strcmp(name, "15NetherDimension") == 0 ? 1 : 2;
        std::memcpy(key_bytes + key_size, &dimension_id, sizeof(dimension_id));
        key_size += sizeof(dimension_id);
    }
    key_bytes[key_size++] = 0x36;
    const std::int32_t finalized = 2;
    auto key = small_string(key_bytes, key_size);
    auto value = small_string(&finalized, sizeof(finalized));
    auto **vtable = *reinterpret_cast<void ***>(batch);
    using Put = void (*)(void *, void *, void *, int);
    reinterpret_cast<Put>(vtable[kWriteBatchPutSlot])(
        batch, &key, &value, 4);
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
    return ChunkSource(chunk_source, dimension, base);
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


void ChunkSource::begin_persistence()
{
    storage_ = db_storage(handle_, base_);
    auto **vtable = *reinterpret_cast<void ***>(handle_);
    using FlushThreadBatch = void (*)(void *);
    reinterpret_cast<FlushThreadBatch>(
        vtable[kFlushThreadBatchSlot])(handle_);
    batch_ = current_write_batch(base_);
}


void ChunkSource::serialize(void *chunk) const
{
    if (chunk == nullptr) {
        throw std::invalid_argument("cannot persist a null LevelChunk");
    }
    const auto base = base_;
    auto *storage = storage_;
    auto *batch = batch_;
    using SetChunkFinalized = void (*)(void *, int);
    reinterpret_cast<SetChunkFinalized>(
        base + kSetChunkFinalizedTarget)(chunk, 2);
    using MarkChunkDirty = void (*)(void *);
    reinterpret_cast<MarkChunkDirty>(base + kMarkChunkDirtyTarget)(chunk);
    using SerializeChunk = void (*)(void *, void *, void *, bool);
    reinterpret_cast<SerializeChunk>(base + kSerializeChunkTarget)(
        storage, chunk, batch, false);
    write_finalized_record(batch, chunk, dimension_, base);
}

void ChunkSource::commit_persistence() const
{
    auto **vtable = *reinterpret_cast<void ***>(handle_);
    using FlushThreadBatch = void (*)(void *);
    reinterpret_cast<FlushThreadBatch>(
        vtable[kFlushThreadBatchSlot])(handle_);
}

}  // namespace chunklet::native
