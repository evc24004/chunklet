#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chunklet::native {

inline constexpr char kSupportedBuildId[] =
    "c0ff36e2eacfc191f01e189237df71f139b5d04a";

inline constexpr std::uintptr_t kOverworldVtable = 0xeea3358;
inline constexpr std::uintptr_t kBlockSourceVtable = 0xf2ea710;
inline constexpr std::uintptr_t kMainChunkSourceVtable = 0xf2efae0;
inline constexpr std::uintptr_t kNetworkChunkSourceVtable = 0xf2ee238;
inline constexpr std::uintptr_t kWorldLimitChunkSourceVtable = 0xf2ee408;
inline constexpr std::uintptr_t kDbChunkStorageVtable = 0xf35b9c8;
inline constexpr std::uintptr_t kLevelStorageWriteBatchVtable = 0xf359ea8;

inline constexpr std::uintptr_t kOverworldTypeNameSlot = 0xeea3770;
inline constexpr std::uintptr_t kBlockSourceTypeNameSlot = 0xf2eb460;
inline constexpr std::uintptr_t kMainChunkSourceTypeNameSlot = 0xf2efd18;
inline constexpr std::uintptr_t kNetworkChunkSourceTypeNameSlot = 0xf2ee638;
inline constexpr std::uintptr_t kWorldLimitChunkSourceTypeNameSlot = 0xf2ee6e0;

inline constexpr std::uintptr_t kDimensionBlockSourceTarget = 0xc965200;
inline constexpr std::uintptr_t kBlockSourceChunkSourceTarget = 0xc8d1130;
inline constexpr std::uintptr_t kMainGetOrLoadTarget = 0xca314e0;
inline constexpr std::uintptr_t kNetworkGetOrLoadTarget = 0xc989e70;
inline constexpr std::uintptr_t kFlushThreadBatchTarget = 0xca32d50;
inline constexpr std::uintptr_t kSerializeChunkTarget = 0xd14bdc0;
inline constexpr std::uintptr_t kMarkChunkDirtyTarget = 0xc9fc600;
inline constexpr std::uintptr_t kSetChunkFinalizedTarget = 0xc9da380;
inline constexpr std::uintptr_t kWriteBatchPutTarget = 0xd0d00a0;

inline constexpr int kDimensionBlockSourceSlot = 13;
inline constexpr int kBlockSourceChunkSourceSlot = 53;
inline constexpr int kGetOrLoadSlot = 9;
inline constexpr int kFlushThreadBatchSlot = 29;
inline constexpr int kWriteBatchPutSlot = 4;
inline constexpr int kDeferredLoadMode = 1;
inline constexpr int kLoadedState = 11;
inline constexpr std::size_t kChunkStateOffset = 0xb8;
inline constexpr std::size_t kEndstoneDimensionHandleOffset = 8;
inline constexpr std::size_t kWriteBatchTlsOffset = 0xb30;
inline constexpr std::size_t kWriteBatchInitializedTlsOffset = 0xab8;

[[nodiscard]] std::uintptr_t executable_base();
[[nodiscard]] std::string executable_build_id();
void verify_layout();
[[nodiscard]] int chunk_state(const void *chunk);

}  // namespace chunklet::native
