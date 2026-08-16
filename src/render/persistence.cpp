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
        source_.begin_persistence();
        std::size_t pending = 0;
        for (const auto &lease : loaded_) {
            if (!lease.required) {
                continue;
            }
            source_.serialize(lease.chunk.get());
            if (++pending == kFlushBatch) {
                source_.commit_persistence();
                pending = 0;
            }
        }
        if (pending != 0) {
            source_.commit_persistence();
        }
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
