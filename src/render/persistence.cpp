#include "render/job.h"

#include <exception>
#include <utility>

namespace chunklet::render {
namespace {

constexpr std::size_t kFlushBatch = 4096;

}  // namespace

LoadEventResult RenderJob::finalize_persistence(std::string &error)
{
    try {
        if (persistence_pending_ != 0) {
            const auto persistence_started = Clock::now();
            source_.commit_persistence();
            persistence_seconds_ +=
                std::chrono::duration<double>(Clock::now() - persistence_started).count();
            persistence_pending_ = 0;
        }
        const auto elapsed =
            std::chrono::duration<double>(Clock::now() - started_).count();
        generation_seconds_ = elapsed - request_seconds_ - persistence_seconds_;
        complete();
        return LoadEventResult::Finished;
    } catch (const std::exception &exception) {
        fail(std::string("native chunk persistence failed: ") + exception.what(), error);
        return LoadEventResult::Failed;
    } catch (...) {
        fail("native chunk persistence failed with an unknown error", error);
        return LoadEventResult::Failed;
    }
}

bool RenderJob::persist(ActiveLease &lease, std::string &error)
{
    const auto persistence_started = Clock::now();
    try {
        source_.serialize(lease.chunk.get());
        if (++persistence_pending_ == kFlushBatch) {
            source_.commit_persistence();
            persistence_pending_ = 0;
        }
        persistence_seconds_ +=
            std::chrono::duration<double>(Clock::now() - persistence_started).count();
        return true;
    } catch (const std::exception &exception) {
        persistence_seconds_ +=
            std::chrono::duration<double>(Clock::now() - persistence_started).count();
        fail(std::string("native chunk persistence failed: ") + exception.what(), error);
        return false;
    } catch (...) {
        persistence_seconds_ +=
            std::chrono::duration<double>(Clock::now() - persistence_started).count();
        fail("native chunk persistence failed with an unknown error", error);
        return false;
    }
}

void RenderJob::complete()
{
    loaded_.clear();
    active_.clear();
    cursor_ = positions_.size();
    finished_ = true;
    stopped_ = Clock::now();
}

void RenderJob::fail(std::string message, std::string &error)
{
    error = std::move(message);
    loaded_.clear();
    active_.clear();
    failed_ = true;
    stopped_ = Clock::now();
}

void RenderJob::cancel()
{
    if (finished_ || failed_) {
        return;
    }
    loaded_.clear();
    active_.clear();
    stopped_ = Clock::now();
    finished_ = true;
}

}  // namespace chunklet::render
