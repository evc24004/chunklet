#include "plugin/chunklet_plugin.h"

#include "native/layout.h"

#include <format>

namespace chunklet {

void ChunkletPlugin::onEnable()
{
    native::verify_layout();
    registerEvent(&ChunkletPlugin::onChunkLoad, *this);
    getLogger().info("Native chunk loader ready: BDS build ID {}.",
                     native::kSupportedBuildId);
}

void ChunkletPlugin::onDisable()
{
    if (job_) {
        job_->cancel();
        last_ = job_->snapshot();
        job_.reset();
    }
}

void ChunkletPlugin::onChunkLoad(const endstone::ChunkLoadEvent &event)
{
    if (!job_ || job_->dimension() != &event.getDimension()) {
        return;
    }
    const auto &chunk = event.getChunk();
    std::string error;
    const auto result = job_->on_chunk_loaded({chunk.getX(), chunk.getZ()}, error);
    if (result == render::LoadEventResult::Finished) {
        finishActive();
    } else if (result == render::LoadEventResult::Failed) {
        failActive(error);
    }
}

void ChunkletPlugin::finishActive()
{
    if (!job_) {
        return;
    }
    last_ = job_->snapshot();
    getLogger().info("Complete. {}", format(*last_));
    job_.reset();
}

void ChunkletPlugin::failActive(const std::string &error)
{
    if (!job_) {
        return;
    }
    last_ = job_->snapshot();
    getLogger().error("Render failed: {}. {}", error, format(*last_));
    job_.reset();
}

std::string ChunkletPlugin::format(const render::JobSnapshot &snapshot)
{
    const double percent = snapshot.total == 0
                               ? 100.0
                               : 100.0 * static_cast<double>(snapshot.completed) /
                                     static_cast<double>(snapshot.total);
    return std::format(
        "{}/{} chunks ({:.1f}%), {:.1f} chunks/s, {:.2f} s, "
        "{} active, {} queued, {} preloaded; native states: {} unloaded, "
        "{} generating, {} processing ({})",
        snapshot.completed, snapshot.total, percent, snapshot.chunks_per_second,
        snapshot.elapsed_seconds, snapshot.active, snapshot.queued,
        snapshot.preloaded, snapshot.native_unloaded, snapshot.native_generating,
        snapshot.native_processing, snapshot.native_state_counts);
}

}  // namespace chunklet

ENDSTONE_PLUGIN("chunklet", "0.1.0", chunklet::ChunkletPlugin)
{
    description = "Pre-generate Bedrock chunks with the native BDS chunk loader.";
    website = "https://github.com/GatewayHoldingsLLC/endstone-chunklet";
    authors = {"Chunklet contributors"};
    load = endstone::PluginLoadOrder::PostWorld;
    prefix = "Chunklet";
    command("chunklet")
        .description("Start, stop, or inspect native chunk generation.")
        .usages("/chunklet <action: string> [radius: int] [dimension: string] "
                "[center-x: int] [center-z: int]");
}
