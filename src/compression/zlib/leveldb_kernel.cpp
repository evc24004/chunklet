#include "compression/zlib/leveldb_kernel.h"
#include <zlib.h>
#include <array>
#include <limits>
namespace chunklet::compression::zlib {
bool compress_leveldb(int level,const char *input,std::size_t length,std::string &output,bool raw)
{
    if(level < -1 || level > 9 || length>std::numeric_limits<uInt>::max()) return false;
    z_stream stream{};
    if(deflateInit2(&stream,level,Z_DEFLATED,raw?-15:15,8,Z_DEFAULT_STRATEGY)!=Z_OK) return false;
    struct Cleanup { z_stream *stream; ~Cleanup(){deflateEnd(stream);} } cleanup{&stream};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input));
    stream.avail_in=static_cast<uInt>(length);
    std::string encoded;
    encoded.reserve(compressBound(length));
    std::array<char,128*1024> buffer;
    int status;
    do {
        stream.next_out=reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out=buffer.size();
        status=deflate(&stream,Z_FINISH);
        if(status!=Z_OK && status!=Z_STREAM_END) return false;
        encoded.append(buffer.data(),buffer.size()-stream.avail_out);
    } while(status!=Z_STREAM_END);
    output.append(encoded);
    return true;
}
}
