#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "native/chunk_source.h"
#include "render/plan.h"

namespace chunklet::render {

enum class LoadEventResult {
    Ignored,
    Progressed,
    PersistenceReady,
    Finished,
    Failed,
};

struct JobSnapshot {
    std::size_t completed{};
    std::size_t total{};
    std::size_t preloaded{};
    std::size_t active{};
    std::size_t queued{};
    std::size_t native_unloaded{};
    std::size_t native_generating{};
    std::size_t native_processing{};
    std::string native_state_counts;
    double elapsed_seconds{};
    double chunks_per_second{};
    bool finished{};
    bool failed{};
};

class RenderJob {
public:
    RenderJob(void *dimension, native::ChunkSource source, ChunkBounds target,
              std::vector<ChunkPosition> positions, std::size_t window);

    [[nodiscard]] bool start(std::string &error);
    [[nodiscard]] LoadEventResult on_chunk_loaded(ChunkPosition position,
                                                   std::string &error);
    [[nodiscard]] LoadEventResult finalize_persistence(std::string &error);
    void cancel();

    [[nodiscard]] void *dimension() const { return dimension_; }
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] bool awaiting_persistence() const
    {
        return awaiting_persistence_;
    }
    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] JobSnapshot snapshot() const;

private:
    using Clock = std::chrono::steady_clock;
    struct ActiveLease {
        std::shared_ptr<void> chunk;
        ChunkPosition position;
        bool required;
    };

    [[nodiscard]] bool fill_window(std::string &error);
    void complete();
    void fail(std::string message, std::string &error);

    void *dimension_;
    native::ChunkSource source_;
    ChunkBounds target_;
    std::vector<ChunkPosition> positions_;
    std::size_t cursor_{};
    std::size_t window_;
    std::vector<ActiveLease> loaded_;
    std::unordered_map<std::uint64_t, ActiveLease> active_;
    std::size_t completed_{};
    std::size_t preloaded_{};
    Clock::time_point started_{Clock::now()};
    Clock::time_point stopped_{};
    bool awaiting_persistence_{};
    bool finished_{};
    bool failed_{};
};

}  // namespace chunklet::render
