#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace chunklet::native {

inline constexpr char kSupportedBuildId[] =
    "2ae1fef8c3ce6a8ebdc43f96a30c4ab307c5ff82";

inline constexpr std::uintptr_t kOverworldVtable = 0x0e28e0b8;
inline constexpr std::uintptr_t kBlockSourceVtable = 0x0e705a90;
inline constexpr std::uintptr_t kMainChunkSourceVtable = 0x0e708be0;
inline constexpr std::uintptr_t kNetworkChunkSourceVtable = 0x0e708d40;
inline constexpr std::uintptr_t kWorldLimitChunkSourceVtable = 0x0e706f10;

inline constexpr std::uintptr_t kOverworldTypeNameSlot = 0x0e28e8f0;
inline constexpr std::uintptr_t kBlockSourceTypeNameSlot = 0x0e705db8;
inline constexpr std::uintptr_t kMainChunkSourceTypeNameSlot = 0x0e708f60;
inline constexpr std::uintptr_t kNetworkChunkSourceTypeNameSlot = 0x0e708fa0;
inline constexpr std::uintptr_t kWorldLimitChunkSourceTypeNameSlot = 0x0e707818;

inline constexpr std::uintptr_t kDimensionBlockSourceTarget = 0x0c118960;
inline constexpr std::uintptr_t kBlockSourceChunkSourceTarget = 0x0c0df0e0;
inline constexpr std::uintptr_t kMainGetOrLoadTarget = 0x0c191870;
inline constexpr std::uintptr_t kNetworkGetOrLoadTarget = 0x0c1503e0;
inline constexpr std::uintptr_t kSaveLiveChunkTarget = 0x0c193650;
inline constexpr std::uintptr_t kFlushThreadBatchTarget = 0x0c193050;

inline constexpr int kDimensionBlockSourceSlot = 13;
inline constexpr int kBlockSourceChunkSourceSlot = 53;
inline constexpr int kGetOrLoadSlot = 9;
inline constexpr int kSaveLiveChunkSlot = 20;
inline constexpr int kFlushThreadBatchSlot = 29;
inline constexpr int kDeferredLoadMode = 1;
inline constexpr int kLoadedState = 13;
inline constexpr std::size_t kChunkStateOffset = 0xb8;
inline constexpr std::size_t kEndstoneDimensionHandleOffset = 8;

[[nodiscard]] std::uintptr_t executable_base();
[[nodiscard]] std::string executable_build_id();
void verify_layout();
[[nodiscard]] int chunk_state(const void *chunk);

}  // namespace chunklet::native
