#include "compression/zlib/stream_pool.h"

#include <array>
#include <cstddef>

namespace chunklet::compression::zlib::stream_pool {
namespace {

struct CachedStream {
    z_stream stream{};
    Config config{};
    bool occupied{};
};

struct ActiveStream {
    z_stream *stream{};
    Config config{};
};

struct StateHead {
    z_stream *stream;
};

constexpr std::size_t kStreamsPerThread = 4;
thread_local std::array<CachedStream, kStreamsPerThread> cache;
thread_local std::array<ActiveStream, kStreamsPerThread> active_streams;

bool same_config(const Config &left, const Config &right) noexcept
{
    return left.level == right.level && left.method == right.method &&
           left.window_bits == right.window_bits &&
           left.memory_level == right.memory_level &&
           left.strategy == right.strategy;
}

}  // namespace

void track(z_stream *stream, const Config &config) noexcept
{
    for (auto &active : active_streams) {
        if (active.stream == nullptr) {
            active = {stream, config};
            return;
        }
    }
}

bool acquire(z_stream *stream, const Config &config)
{
    for (auto &cached : cache) {
        if (!cached.occupied || !same_config(cached.config, config)) {
            continue;
        }
        *stream = cached.stream;
        reinterpret_cast<StateHead *>(stream->state)->stream = stream;
        cached.occupied = false;
        if (deflateReset(stream) == Z_OK) {
            track(stream, config);
            return true;
        }
        deflateEnd(stream);
        break;
    }
    return false;
}

bool release(z_stream *stream) noexcept
{
    for (auto &active : active_streams) {
        if (active.stream != stream) {
            continue;
        }
        for (auto &cached : cache) {
            if (!cached.occupied) {
                cached.stream = *stream;
                cached.config = active.config;
                cached.occupied = true;
                reinterpret_cast<StateHead *>(cached.stream.state)->stream =
                    &cached.stream;
                active.stream = nullptr;
                return true;
            }
        }
        active.stream = nullptr;
        return false;
    }
    return false;
}

}  // namespace chunklet::compression::zlib::stream_pool
