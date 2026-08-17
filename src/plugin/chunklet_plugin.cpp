#include "plugin/chunklet_plugin.h"

#include "allocation/transient/object_pool.h"
#include "cache/ownership/optimizer.h"
#include "synchronization/shared_mutex/optimizer.h"
#include "native/layout.h"
#include "noise/area/optimizer.h"
#include "noise/construction/direct.h"
#include "noise/construction/reserve.h"
#include "noise/perlin/octave_optimizer.h"
#include "noise/perlin/optimizer.h"
#include "noise/shuffle/optimizer.h"
#include "spatial/proximity/optimizer.h"
#include "timing/monotonic/optimizer.h"

#include <format>

namespace chunklet {

void ChunkletPlugin::onLoad()
{
    native::verify_layout();
    noise::area::install();
    try {
        timing::monotonic::install();
        cache::ownership::install();
        synchronization::shared_mutex::install();
        allocation::transient::install();
        noise::construction::install();
        spatial::proximity::install();
        noise::construction::install_direct();
        noise::perlin::install();
        noise::perlin::install_octaves();
        noise::shuffle::install();
    } catch (...) {
        noise::shuffle::remove();
        noise::perlin::remove_octaves();
        noise::perlin::remove();
        noise::construction::remove_direct();
        spatial::proximity::remove();
        noise::construction::remove();
        allocation::transient::remove();
        synchronization::shared_mutex::remove();
        cache::ownership::remove();
        timing::monotonic::remove();
        noise::area::remove();
        throw;
    }
}

void ChunkletPlugin::onEnable()
{
    registerEvent(&ChunkletPlugin::onChunkLoad, *this);
    getLogger().info(
        "Native chunk loader, cached ownership resolution, scalable shared mutexes, "
        "transient terrain object pool, validated direct noise construction, "
        "preallocated noise output, fused noise shuffle, and validated AVX2 area, "
        "proximity, Perlin, and octave evaluation ready: BDS build ID {}.",
        native::kSupportedBuildId);
}

void ChunkletPlugin::onDisable()
{
    if (job_) {
        job_->cancel();
        last_ = job_->snapshot();
        job_.reset();
    }
    const auto area_mismatches = noise::area::mismatch_count();
    if (area_mismatches != 0) {
        getLogger().error(
            "AVX2 area evaluator disabled after {} bit-exact validation mismatch(es).",
            area_mismatches);
    } else {
        getLogger().info(
            "AVX2 area evaluator: {} bit-exact validations.",
            noise::area::validation_count());
    }
    const auto proximity_mismatches = spatial::proximity::mismatch_count();
    if (proximity_mismatches != 0) {
        getLogger().error(
            "AVX2 proximity evaluator disabled after {} exact mismatch(es).",
            proximity_mismatches);
    } else {
        getLogger().info(
            "AVX2 proximity evaluator: {} exact validations.",
            spatial::proximity::validation_count());
    }
    const auto perlin_mismatches = noise::perlin::mismatch_count();
    if (perlin_mismatches != 0) {
        getLogger().error(
            "AVX2 Perlin evaluator disabled after {} bit-exact validation mismatch(es).",
            perlin_mismatches);
    } else {
        getLogger().info(
            "AVX2 Perlin evaluator: {} bit-exact validations.",
            noise::perlin::validation_count());
    }
    const auto octave_mismatches = noise::perlin::octave_mismatch_count();
    if (octave_mismatches != 0) {
        getLogger().error(
            "AVX2 octave evaluator disabled after {} bit-exact validation mismatch(es).",
            octave_mismatches);
    } else {
        getLogger().info(
            "AVX2 octave evaluator: {} bit-exact validations.",
            noise::perlin::octave_validation_count());
    }
    const auto construction = noise::construction::direct_stats();
    if (construction.mismatches != 0) {
        getLogger().error(
            "Direct noise construction disabled after {} bit-exact mismatch(es): "
            "mode={}, element={}, offset=0x{:x}, expected=0x{:x}, actual=0x{:x}.",
            construction.mismatches, construction.mismatch_mode,
            construction.mismatch_element, construction.mismatch_offset,
            construction.mismatch_expected, construction.mismatch_actual);
    } else {
        getLogger().info(
            "Direct noise construction: {} validations from {} calls; "
            "{} mode-zero, {} recognized-random, {} supported-shape.",
            construction.validations, construction.calls, construction.mode_zero,
            construction.random_matches, construction.shape_matches);
        getLogger().info(
            "Mode-zero random methods: nextInt=0x{:x}, nextDouble=0x{:x}, "
            "consume=0x{:x}.",
            construction.next_int, construction.next_double, construction.consume);
    }
    const auto clock_mismatches = timing::monotonic::mismatch_count();
    if (clock_mismatches != 0) {
        getLogger().error(
            "TSC monotonic clock disabled after {} validation mismatch(es); "
            "maximum error={} ns.",
            clock_mismatches, timing::monotonic::maximum_error_ns());
    } else {
        getLogger().info(
            "TSC monotonic clock: {} validations, maximum error={} ns.",
            timing::monotonic::validation_count(),
            timing::monotonic::maximum_error_ns());
    }
    noise::shuffle::remove();
    noise::perlin::remove_octaves();
    noise::perlin::remove();
    noise::construction::remove_direct();
    spatial::proximity::remove();
    noise::construction::remove();
    allocation::transient::remove();
    synchronization::shared_mutex::remove();
    cache::ownership::remove();
    timing::monotonic::remove();
    noise::area::remove();
}

void ChunkletPlugin::onChunkLoad(const endstone::ChunkLoadEvent &event)
{
    if (!job_ || job_->dimension() != &event.getDimension()) {
        return;
    }
    const auto &chunk = event.getChunk();
    std::string error;
    const auto result = job_->on_chunk_loaded({chunk.getX(), chunk.getZ()}, error);
    if (result == render::LoadEventResult::PersistenceReady) {
        persistActive();
    } else if (result == render::LoadEventResult::Finished) {
        finishActive();
    } else if (result == render::LoadEventResult::Failed) {
        failActive(error);
    }
}

void ChunkletPlugin::persistActive()
{
    if (!job_ || !job_->awaiting_persistence()) {
        return;
    }
    std::string error;
    const auto result = job_->finalize_persistence(error);
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
        "{} active, {} queued, {} preloaded; phases: {:.2f} s request, "
        "{:.2f} s generation, {:.2f} s persistence; native states: {} unloaded, "
        "{} generating, {} processing ({})",
        snapshot.completed, snapshot.total, percent, snapshot.chunks_per_second,
        snapshot.elapsed_seconds, snapshot.active, snapshot.queued,
        snapshot.preloaded, snapshot.request_seconds, snapshot.generation_seconds,
        snapshot.persistence_seconds, snapshot.native_unloaded,
        snapshot.native_generating, snapshot.native_processing,
        snapshot.native_state_counts);
}

}  // namespace chunklet

ENDSTONE_PLUGIN("chunklet", "0.1.4", chunklet::ChunkletPlugin)
{
    description = "Pre-generate Bedrock chunks with the native BDS chunk loader.";
    website = "https://github.com/evc24004/chunklet";
    authors = {"Chunklet contributors"};
    load = endstone::PluginLoadOrder::PostWorld;
    prefix = "Chunklet";
    command("chunklet")
        .description("Start, stop, or inspect native chunk generation.")
        .usages("/chunklet <action: string> [radius: int] [dimension: string] "
                "[center-x: int] [center-z: int]");
}
