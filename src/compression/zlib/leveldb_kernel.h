#pragma once
#include <cstddef>
#include <string>
namespace chunklet::compression::zlib {
// Appends only after successful completion. No stream escapes this call.
bool compress_leveldb(int level,const char *input,std::size_t length,std::string &output,bool raw);
}
