#include "render/job.h"

#include "native/layout.h"

#include <array>
#include <algorithm>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

namespace chunklet::render {

RenderJob::RenderJob(void *dimension, native::ChunkSource source, ChunkBounds target,
                     std::vector<ChunkPosition> positions, std::size_t window)
    : dimension_(dimension), source_(source), target_(target),
      positions_(std::move(positions)), window_(window)
{
    if (dimension_ == nullptr || !source_.valid()) {
        throw std::invalid_argument("render job requires a dimension and chunk source");
    }
    if (window_ == 0) {
        throw std::invalid_argument("render window must be positive");
    }
    active_.reserve(std::min(window_, positions_.size()));
    loaded_.reserve(positions_.size());
}

bool RenderJob::start(std::string &error)
{
    started_ = Clock::now();
    try {
        const auto persistence_started = Clock::now();
        source_.begin_persistence();
        persistence_seconds_ +=
            std::chrono::duration<double>(Clock::now() - persistence_started).count();
    } catch (const std::exception &exception) {
        fail(std::string("native chunk persistence failed: ") + exception.what(), error);
        return false;
    } catch (...) {
        fail("native chunk persistence failed with an unknown error", error);
        return false;
    }
    const bool started = fill_window(error);
    requests_finished_ = Clock::now();
    request_seconds_ =
        std::chrono::duration<double>(requests_finished_ - started_).count() -
        persistence_seconds_;
    return started;
}

LoadEventResult RenderJob::on_chunk_loaded(ChunkPosition position, std::string &error)
{
    if (finished_ || failed_) {
        return LoadEventResult::Ignored;
    }
    if (awaiting_persistence_) {
        return LoadEventResult::Ignored;
    }
    const auto found = active_.find(chunk_key(position));
    if (found == active_.end()) {
        return LoadEventResult::Ignored;
    }
    if (native::chunk_state(found->second.chunk.get()) != native::kLoadedState) {
        fail(std::format("chunk {},{} emitted ChunkLoadEvent in native state {}",
                         position.x, position.z,
                         native::chunk_state(found->second.chunk.get())), error);
        return LoadEventResult::Failed;
    }
    const bool required = found->second.required;
    if (required && !persist(found->second, error)) {
        return LoadEventResult::Failed;
    }
    loaded_.push_back(std::move(found->second));
    active_.erase(found);
    completed_ += required;
    if (completed_ == target_.count()) {
        awaiting_persistence_ = true;
        return LoadEventResult::PersistenceReady;
    }

    if (!fill_window(error)) {
        return LoadEventResult::Failed;
    }
    return LoadEventResult::Progressed;
}

bool RenderJob::fill_window(std::string &error)
{
    try {
        while (cursor_ < positions_.size() && active_.size() < window_) {
            const auto position = positions_[cursor_++];
            auto chunk = source_.request(position);
            if (!chunk) {
                fail(std::format("native chunk request returned null at {},{}",
                                 position.x, position.z), error);
                return false;
            }
            const bool required = target_.contains(position);
            if (native::chunk_state(chunk.get()) == native::kLoadedState) {
                ActiveLease lease{std::move(chunk), position, required};
                if (required && !persist(lease, error)) {
                    return false;
                }
                completed_ += required;
                preloaded_ += required;
                loaded_.push_back(std::move(lease));
                awaiting_persistence_ = completed_ == target_.count();
                continue;
            }
            active_.emplace(chunk_key(position),
                            ActiveLease{std::move(chunk), position, required});
        }
        return true;
    } catch (const std::exception &exception) {
        fail(std::string("native chunk request failed: ") + exception.what(), error);
        return false;
    } catch (...) {
        fail("native chunk request failed with an unknown error", error);
        return false;
    }
}


JobSnapshot RenderJob::snapshot() const
{
    const auto end = finished_ || failed_ ? stopped_ : Clock::now();
    const auto elapsed = std::chrono::duration<double>(end - started_).count();
    const auto rate = elapsed > 0.0 ? static_cast<double>(completed_) / elapsed : 0.0;
    std::size_t unloaded = 0;
    std::array<std::size_t, 14> state_counts{};
    std::size_t generating = 0;
    std::size_t processing = 0;
    for (const auto &[key, lease] : active_) {
        static_cast<void>(key);
        const auto state = native::chunk_state(lease.chunk.get());
        if (state >= 0 && state < static_cast<int>(state_counts.size())) {
            ++state_counts[static_cast<std::size_t>(state)];
        }
        unloaded += state == 0;
        generating += state == 1;
        processing += state >= 2 && state < native::kLoadedState;
    }
    std::string state_text;
    for (std::size_t state = 0; state < state_counts.size(); ++state) {
        if (state_counts[state] != 0) {
            if (!state_text.empty()) {
                state_text += ',';
            }
            state_text += std::to_string(state) + ':' + std::to_string(state_counts[state]);
        }
    }
    return {
        .completed = completed_,
        .total = static_cast<std::size_t>(target_.count()),
        .preloaded = preloaded_,
        .active = active_.size(),
        .queued = positions_.size() - cursor_,
        .native_unloaded = unloaded,
        .native_generating = generating,
        .native_processing = processing,
        .native_state_counts = std::move(state_text),
        .elapsed_seconds = elapsed,
        .chunks_per_second = rate,
        .request_seconds = request_seconds_,
        .generation_seconds = generation_seconds_,
        .persistence_seconds = persistence_seconds_,
        .finished = finished_,
        .failed = failed_,
    };
}

}  // namespace chunklet::render
