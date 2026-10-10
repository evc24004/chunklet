#pragma once
#include <cstddef>
#include <cstdint>
namespace chunklet::compression::zlib::leveldb {
struct Stats { std::size_t calls,validations,mismatches; std::uint64_t input_bytes,native_bytes,candidate_bytes,native_ns,candidate_ns; };
void install();
void remove() noexcept;
Stats stats() noexcept;
}
