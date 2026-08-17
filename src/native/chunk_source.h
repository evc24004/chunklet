#pragma once

#include <cstdint>
#include <memory>

#include "render/plan.h"

namespace chunklet::native {

class ChunkSource {
public:
    ChunkSource() = default;

    [[nodiscard]] static ChunkSource resolve(void *endstone_dimension);
    [[nodiscard]] std::shared_ptr<void> request(render::ChunkPosition position) const;
    void begin_persistence();
    void serialize(void *chunk) const;
    void commit_persistence() const;
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }

private:
    ChunkSource(void *handle, void *dimension, std::uintptr_t base)
        : handle_(handle), dimension_(dimension), base_(base) {}

    void *handle_{};
    void *dimension_{};
    std::uintptr_t base_{};
    void *storage_{};
    void *batch_{};
};

}  // namespace chunklet::native
