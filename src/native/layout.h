#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chunklet::native {

inline constexpr char kSupportedBuildId[] =
    "52576b75b6af5a09a6bde582be804e29657dbbf3";

inline constexpr std::uintptr_t kOverworldVtable = 0xf717938;
inline constexpr std::uintptr_t kBlockSourceVtable = 0xfbc8050;
inline constexpr std::uintptr_t kMainChunkSourceVtable = 0xfbcee98;
inline constexpr std::uintptr_t kNetworkChunkSourceVtable = 0xfbcd670;
inline constexpr std::uintptr_t kWorldLimitChunkSourceVtable = 0xfbcd840;
inline constexpr std::uintptr_t kDbChunkStorageVtable = 0xfc40ce0;
inline constexpr std::uintptr_t kLevelStorageWriteBatchVtable = 0xfc40bc8;

inline constexpr std::uintptr_t kOverworldTypeNameSlot = 0xf7181a8;
inline constexpr std::uintptr_t kBlockSourceTypeNameSlot = 0xfbc8488;
inline constexpr std::uintptr_t kMainChunkSourceTypeNameSlot = 0xfbcf0d0;
inline constexpr std::uintptr_t kNetworkChunkSourceTypeNameSlot = 0xfbcda70;
inline constexpr std::uintptr_t kWorldLimitChunkSourceTypeNameSlot = 0xfbcdb18;

inline constexpr std::uintptr_t kDimensionBlockSourceTarget = 0xcf1c8f0;
inline constexpr std::uintptr_t kBlockSourceChunkSourceTarget = 0xce574b0;
inline constexpr std::uintptr_t kMainGetOrLoadTarget = 0xcfe0bc0;
inline constexpr std::uintptr_t kNetworkGetOrLoadTarget = 0xcf41e80;
inline constexpr std::uintptr_t kFlushThreadBatchTarget = 0xcfe2100;
inline constexpr std::uintptr_t kSerializeChunkTarget = 0xd713c70;
inline constexpr std::uintptr_t kMarkChunkDirtyTarget = 0xcfadeb0;
inline constexpr std::uintptr_t kSetChunkFinalizedTarget = 0xcf8c8f0;
inline constexpr std::uintptr_t kWriteBatchPutTarget = 0xd6ea9b0;

inline constexpr int kDimensionBlockSourceSlot = 14;
inline constexpr int kBlockSourceChunkSourceSlot = 53;
inline constexpr int kGetOrLoadSlot = 9;
inline constexpr int kFlushThreadBatchSlot = 29;
inline constexpr int kWriteBatchPutSlot = 4;
inline constexpr int kDeferredLoadMode = 1;
inline constexpr int kLoadedState = 11;
inline constexpr std::size_t kChunkStateOffset = 0xb8;
inline constexpr std::size_t kEndstoneDimensionHandleOffset = 8;
inline constexpr std::size_t kWriteBatchTlsOffset = 0xb78;
inline constexpr std::size_t kWriteBatchInitializedTlsOffset = 0xb00;

[[nodiscard]] std::uintptr_t executable_base();
[[nodiscard]] std::string executable_build_id();
void verify_layout();
[[nodiscard]] int chunk_state(const void *chunk);

}  // namespace chunklet::native
