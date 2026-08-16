#pragma once

#include <memory>

#include "render/plan.h"

namespace chunklet::native {

class ChunkSource {
public:
    ChunkSource() = default;

    [[nodiscard]] static ChunkSource resolve(void *endstone_dimension);
    [[nodiscard]] std::shared_ptr<void> request(render::ChunkPosition position) const;
    [[nodiscard]] bool save(void *chunk) const;
    void flush() const;
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }

private:
    explicit ChunkSource(void *handle) : handle_(handle) {}

    void *handle_{};
};

}  // namespace chunklet::native
