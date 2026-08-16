#pragma once

#include <memory>

#include "render/plan.h"

namespace chunklet::native {

class ChunkSource {
public:
    ChunkSource() = default;

    [[nodiscard]] static ChunkSource resolve(void *endstone_dimension);
    [[nodiscard]] std::shared_ptr<void> request(render::ChunkPosition position) const;
    void begin_persistence() const;
    void serialize(void *chunk) const;
    void commit_persistence() const;
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }

private:
    ChunkSource(void *handle, void *dimension)
        : handle_(handle), dimension_(dimension) {}

    void *handle_{};
    void *dimension_{};
};

}  // namespace chunklet::native
