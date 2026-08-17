#pragma once

#include <zlib.h>

#include <cstdint>

namespace chunklet::compression::zlib::stream_pool {

struct Config {
    std::int32_t level;
    std::int32_t method;
    std::int32_t window_bits;
    std::int32_t memory_level;
    std::int32_t strategy;
};

bool acquire(z_stream *stream, const Config &config);
void track(z_stream *stream, const Config &config) noexcept;
bool release(z_stream *stream) noexcept;

}  // namespace chunklet::compression::zlib::stream_pool
