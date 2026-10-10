// Usage: build via tools/build-zlib-proof.sh BUILD_DIR; proves raw/wrapped
// zlib-ng compression decodes with independent system zlib and benchmarks both.
#include "compression/zlib/leveldb_kernel.h"
#include <zlib.h>
#include <dlfcn.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>
int main() {
    void *system=dlopen("libz.so.1",RTLD_NOW|RTLD_LOCAL); assert(system);
    auto init=reinterpret_cast<int(*)(z_streamp,int,const char*,int)>(dlsym(system,"inflateInit2_"));
    auto step=reinterpret_cast<int(*)(z_streamp,int)>(dlsym(system,"inflate"));
    auto end=reinterpret_cast<int(*)(z_streamp)>(dlsym(system,"inflateEnd"));
    auto def_init=reinterpret_cast<int(*)(z_streamp,int,int,int,int,int,const char*,int)>(dlsym(system,"deflateInit2_"));
    auto def_step=reinterpret_cast<int(*)(z_streamp,int)>(dlsym(system,"deflate"));
    auto def_end=reinterpret_cast<int(*)(z_streamp)>(dlsym(system,"deflateEnd"));
    assert(init&&step&&end&&def_init&&def_step&&def_end);
    std::mt19937 rng(42); std::vector<std::string> samples={"", "hello",std::string(131073,'a'),std::string(262144,'\0'),std::string(262144,'\0')};
    for(size_t i=0;i<samples[3].size();++i){samples[3][i]=char(rng());samples[4][i]=char((i/64+i%17)%31);}
    unsigned cases=0;
    for(bool raw:{false,true}) for(int level:{-1,0,1,6,9}) for(const auto& input:samples){
        std::string encoded="prefix";
        assert(chunklet::compression::zlib::compress_leveldb(level,input.data(),input.size(),encoded,raw));
        assert(encoded.starts_with("prefix"));
        z_stream stream{}; assert(init(&stream,raw?-15:15,ZLIB_VERSION,sizeof(stream))==Z_OK);
        std::string decoded(input.size()+1,'\0');stream.next_in=reinterpret_cast<Bytef*>(encoded.data()+6);stream.avail_in=encoded.size()-6;stream.next_out=reinterpret_cast<Bytef*>(decoded.data());stream.avail_out=decoded.size();
        assert(step(&stream,Z_FINISH)==Z_STREAM_END);assert(stream.avail_in==0);assert(stream.total_out==input.size());assert(decoded.substr(0,input.size())==input);assert(end(&stream)==Z_OK);++cases;
    }
    std::string untouched="keep";assert(!chunklet::compression::zlib::compress_leveldb(10,"x",1,untouched,true));assert(untouched=="keep");
    for(size_t sample:{2,3,4}){
        const auto& input=samples[sample];size_t sizes[2]{};double seconds[2]{};
        for(int provider=0;provider<2;++provider){auto start=std::chrono::steady_clock::now();for(int round=0;round<200;++round){std::string out;if(provider){assert(chunklet::compression::zlib::compress_leveldb(6,input.data(),input.size(),out,true));}else{z_stream s{};assert(def_init(&s,6,Z_DEFLATED,-15,8,Z_DEFAULT_STRATEGY,ZLIB_VERSION,sizeof(s))==Z_OK);out.resize(input.size()*2+1024);s.next_in=(Bytef*)input.data();s.avail_in=input.size();s.next_out=(Bytef*)out.data();s.avail_out=out.size();assert(def_step(&s,Z_FINISH)==Z_STREAM_END);out.resize(s.total_out);assert(def_end(&s)==Z_OK);}sizes[provider]=out.size();}seconds[provider]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
        std::cout<<"sample="<<sample<<" input_bytes="<<input.size()<<" stock_bytes="<<sizes[0]<<" ng_bytes="<<sizes[1]<<" stock_seconds="<<seconds[0]<<" ng_seconds="<<seconds[1]<<" speedup="<<seconds[0]/seconds[1]<<'\n';
    }
    std::cout<<"PASS: "<<cases<<" raw/wrapped wire-compatibility cases with independent system inflate\n";
}
