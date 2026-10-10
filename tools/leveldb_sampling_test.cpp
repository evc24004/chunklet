// Usage: tools/build-zlib-proof.sh BUILD_DIR (also runs this focused regression).
// Exercise the real hook and zlib-ng kernel, using independently loaded libz
// at the native ABI boundary. Inject one compressor failure to model ENOMEM.
#include "compression/zlib/leveldb_kernel.h"
#include <atomic>
namespace chunklet::compression::zlib {
bool fixture_compress(int, const char *, std::size_t, std::string &, bool);
}
#define compress_leveldb fixture_compress
#include "compression/zlib/leveldb_optimizer.cpp"
#undef compress_leveldb
#include <dlfcn.h>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

std::atomic<bool> fail_next{};
std::atomic<bool> hold_samples{};
std::atomic<unsigned> entered{}, completed{};
bool chunklet::compression::zlib::fixture_compress(
    int level, const char *input, std::size_t length, std::string &output, bool raw)
{
    if (fail_next.exchange(false)) return false;
    if (hold_samples.load()) {
        entered.fetch_add(1);
        while (hold_samples.load()) std::this_thread::yield();
    }
    return compress_leveldb(level, input, length, output, raw);
}

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// Native reference owns its stream; the fixture never shares private zlib state.
void *system_zlib;
bool reference_compress(int level, const char *input, std::size_t length,
                        std::string &output, bool raw)
{
    using Init = int (*)(z_stream *, int, int, int, int, int, const char *, int);
    using Step = int (*)(z_stream *, int);
    using End = int (*)(z_stream *);
    const auto init = reinterpret_cast<Init>(dlsym(system_zlib, "deflateInit2_"));
    const auto step = reinterpret_cast<Step>(dlsym(system_zlib, "deflate"));
    const auto end = reinterpret_cast<End>(dlsym(system_zlib, "deflateEnd"));
    require(init && step && end, "system deflater unavailable");
    z_stream stream{};
    if (init(&stream, level, Z_DEFLATED, raw ? -15 : 15, 8,
             Z_DEFAULT_STRATEGY, ZLIB_VERSION, sizeof(stream)) != Z_OK) return false;
    struct Cleanup { z_stream *stream; End end; ~Cleanup() { end(stream); } } cleanup{&stream, end};
    std::string encoded(length * 2 + 1024, '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input));
    stream.avail_in = length;
    stream.next_out = reinterpret_cast<Bytef *>(encoded.data());
    stream.avail_out = encoded.size();
    require(step(&stream, Z_FINISH) == Z_STREAM_END, "reference compression failed");
    output.append(encoded.data(), stream.total_out);
    return true;
}

int main()
{
    using namespace chunklet::compression::zlib::leveldb;
    system_zlib = dlopen("libz.so.1", RTLD_NOW | RTLD_LOCAL);
    require(system_zlib != nullptr, "system zlib unavailable");
    original = &reference_compress;
    native_init = reinterpret_cast<Init>(dlsym(system_zlib, "inflateInit2_"));
    native_inflate = reinterpret_cast<Inflate>(dlsym(system_zlib, "inflate"));
    native_end = reinterpret_cast<End>(dlsym(system_zlib, "inflateEnd"));
    require(native_init && native_inflate && native_end, "system inflater unavailable");
    const std::string input(4096, 'a');
    std::string output;
    for (int index = 0; index < 16; ++index)
        require(!checked_compress(10, input.data(), input.size(), output, true),
                "unsupported level must retain native failure");
    fail_next = true;
    require(checked_compress(6, input.data(), input.size(), output, true),
            "candidate failure must fall back to native");
    require(validations.load() == 0, "fallback must not count as validation");
    for (int index = 0; index < 17; ++index) {
        output.clear();
        require(checked_compress(6, input.data(), input.size(), output, index % 2),
                "supported input must compress");
        require(native_roundtrip(output, input.data(), input.size(), index % 2),
                "hook output must decode exactly");
    }
    require(validations.load() == 16, "fallback inputs permanently consumed validation slots");
    require(mismatches.load() == 0, "unexpected mismatch");
    validations=0;
    sample_slots=0;
    hold_samples=true;
    std::vector<std::thread> workers;
    for (int index=0;index<24;++index) workers.emplace_back([&input]{
        std::string encoded;
        require(checked_compress(6,input.data(),input.size(),encoded,true),
                "concurrent compression failed");
        require(native_roundtrip(encoded,input.data(),input.size(),true),
                "concurrent compression changed input");
        completed.fetch_add(1);
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while((entered.load()!=16 || completed.load()!=8) &&
          std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    const bool bounded=entered.load()==16 && completed.load()==8 && validations.load()==0;
    hold_samples=false;
    for(auto &worker:workers)worker.join();
    require(bounded,"concurrent samples must reserve at most 16 slots; excess calls use native");
    require(validations.load()==16 && sample_slots.load()==16,"concurrent validation failed to finish");
    std::puts("PASS: fallback inputs preserve 16 validation slots; 24 concurrent calls honor the native fallback gate");
}
