#include "compression/zlib/leveldb_optimizer.h"
#include "compression/zlib/leveldb_kernel.h"
#include "native/layout.h"
#include <zlib.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
namespace chunklet::compression::zlib::leveldb {
namespace {
constexpr std::uintptr_t kCompress=0x40adb40;
constexpr std::uintptr_t kInflateInit=0x46001d0;
constexpr std::uintptr_t kInflate=0x4600440;
constexpr std::uintptr_t kInflateEnd=0x4602dc0;
constexpr std::array<unsigned char,15> kOriginal{0x55,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x81,0xec,0x70,0x00,0x02,0x00};
constexpr std::array<unsigned char,14> kInflateInitOriginal{0xb8,0xfa,0xff,0xff,0xff,0x48,0x85,0xd2,0x0f,0x84,0x97,0x00,0x00,0x00};
constexpr std::array<unsigned char,14> kInflateOriginal{0xb8,0xfe,0xff,0xff,0xff,0x48,0x85,0xff,0x0f,0x84,0x69,0x28,0x00,0x00};
constexpr std::array<unsigned char,15> kInflateEndOriginal{0xb8,0xfe,0xff,0xff,0xff,0x48,0x85,0xff,0x74,0x1e,0x48,0x83,0x7f,0x40,0x00};
using Compress=bool(*)(int,const char*,std::size_t,std::string&,bool);
using Init=int(*)(z_stream*,int,const char*,int);
using Inflate=int(*)(z_stream*,int);
using End=int(*)(z_stream*);
std::uintptr_t base;
void *trampoline;
Compress original;
Init native_init;
Inflate native_inflate;
End native_end;
bool installed;
std::atomic<bool> disabled{};
std::atomic<std::size_t> calls{},validations{},mismatches{};
std::atomic<std::uint64_t> input_bytes{},native_bytes{},candidate_bytes{},native_ns{},candidate_ns{};
constexpr std::size_t kTrampolineSize=29;

template<std::size_t N> void guard(std::uintptr_t address,const std::array<unsigned char,N>& bytes)
{
    if(std::memcmp(reinterpret_cast<void*>(base+address),bytes.data(),N)!=0)
        throw std::runtime_error("BDS LevelDB compression/inflate bytes do not match pinned build");
}

bool native_roundtrip(const std::string& encoded,const char *input,std::size_t length,bool raw)
{
    if(length>=std::numeric_limits<uInt>::max() || encoded.size()>std::numeric_limits<uInt>::max())return false;
    z_stream stream{};
    if(native_init(&stream,raw?-15:15,ZLIB_VERSION,sizeof(stream))!=Z_OK)return false;
    struct Cleanup {z_stream* s;~Cleanup(){native_end(s);}} cleanup{&stream};
    std::string decoded(length+1,'\0');
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(encoded.data()));stream.avail_in=encoded.size();
    stream.next_out=reinterpret_cast<Bytef*>(decoded.data());stream.avail_out=decoded.size();
    return native_inflate(&stream,Z_FINISH)==Z_STREAM_END && stream.avail_in==0 && stream.total_out==length && std::memcmp(decoded.data(),input,length)==0;
}

bool checked_compress(int level,const char* input,std::size_t length,std::string& output,bool raw)
{
    const auto index=calls.fetch_add(1,std::memory_order_relaxed);
    if(disabled.load(std::memory_order_acquire) || level < -1 || level > 9 || length>=std::numeric_limits<uInt>::max())
        return original(level,input,length,output,raw);
    if(index>=16 && validations.load(std::memory_order_acquire)<16)
        return original(level,input,length,output,raw);
    std::string candidate;
    const auto start=index<16?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    if(!compress_leveldb(level,input,length,candidate,raw))return original(level,input,length,output,raw);
    if(index<16){
        const auto midpoint=std::chrono::steady_clock::now();
        std::string reference;
        const bool expected=original(level,input,length,reference,raw);
        const auto finish=std::chrono::steady_clock::now();
        if(!expected || !native_roundtrip(candidate,input,length,raw) || !native_roundtrip(reference,input,length,raw)){
            mismatches.fetch_add(1,std::memory_order_relaxed);disabled.store(true,std::memory_order_release);
            if(expected){output.append(reference);return true;}
            return false;
        }
        validations.fetch_add(1,std::memory_order_release);
        input_bytes.fetch_add(length,std::memory_order_relaxed);
        native_bytes.fetch_add(reference.size(),std::memory_order_relaxed);
        candidate_bytes.fetch_add(candidate.size(),std::memory_order_relaxed);
        native_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-midpoint).count(),std::memory_order_relaxed);
        candidate_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(midpoint-start).count(),std::memory_order_relaxed);
    }
    output.append(candidate);
    return true;
}

void jump(unsigned char* destination,std::uintptr_t target)
{
    destination[0]=0xff;destination[1]=0x25;std::memset(destination+2,0,4);std::memcpy(destination+6,&target,8);
}
void *page(){const auto size=static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));return reinterpret_cast<void*>((base+kCompress)&~(size-1));}
}

void install()
{
    if(installed)return;
    static_assert(sizeof(z_stream)==0x70);
    static_assert(sizeof(std::string)==24);
    base=native::executable_base();
    guard(kCompress,kOriginal);guard(kInflateInit,kInflateInitOriginal);guard(kInflate,kInflateOriginal);guard(kInflateEnd,kInflateEndOriginal);
    native_init=reinterpret_cast<Init>(base+kInflateInit);native_inflate=reinterpret_cast<Inflate>(base+kInflate);native_end=reinterpret_cast<End>(base+kInflateEnd);
    // Before patching, prove this exact server accepts raw and wrapped output.
    for(bool raw:{false,true}) for(int level:{-1,1,6}){
        const std::string input="Chunklet LevelDB wire compatibility: 0123456789 0123456789";
        std::string candidate;
        if(!compress_leveldb(level,input.data(),input.size(),candidate,raw)||!native_roundtrip(candidate,input.data(),input.size(),raw))
            throw std::runtime_error("BDS rejected zlib-ng wire compatibility validation");
    }
    trampoline=mmap(nullptr,kTrampolineSize,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(trampoline==MAP_FAILED){trampoline=nullptr;throw std::runtime_error("LevelDB compression trampoline allocation failed");}
    std::memcpy(trampoline,kOriginal.data(),kOriginal.size());
    jump(static_cast<unsigned char*>(trampoline)+kOriginal.size(),base+kCompress+kOriginal.size());
    if(mprotect(trampoline,kTrampolineSize,PROT_READ|PROT_EXEC)!=0){remove();throw std::runtime_error("LevelDB compression trampoline protection failed");}
    original=reinterpret_cast<Compress>(trampoline);
    const auto page_size=sysconf(_SC_PAGESIZE);
    if(mprotect(page(),page_size,PROT_READ|PROT_WRITE|PROT_EXEC)!=0){remove();throw std::runtime_error("LevelDB compression patch protection failed");}
    std::array<unsigned char,15> patch{};patch.fill(0x90);jump(patch.data(),reinterpret_cast<std::uintptr_t>(&checked_compress));
    std::memcpy(reinterpret_cast<void*>(base+kCompress),patch.data(),patch.size());
    __builtin___clear_cache(reinterpret_cast<char*>(base+kCompress),reinterpret_cast<char*>(base+kCompress+patch.size()));
    installed=true;
    if(mprotect(page(),page_size,PROT_READ|PROT_EXEC)!=0){remove();throw std::runtime_error("LevelDB compression patch final protection failed");}
}

void remove() noexcept
{
    if(installed){
        const auto size=sysconf(_SC_PAGESIZE);
        if(mprotect(page(),size,PROT_READ|PROT_WRITE|PROT_EXEC)!=0)return;
        std::memcpy(reinterpret_cast<void*>(base+kCompress),kOriginal.data(),kOriginal.size());
        __builtin___clear_cache(reinterpret_cast<char*>(base+kCompress),reinterpret_cast<char*>(base+kCompress+kOriginal.size()));
        mprotect(page(),size,PROT_READ|PROT_EXEC);installed=false;
    }
    if(trampoline){munmap(trampoline,kTrampolineSize);trampoline=nullptr;original=nullptr;}
}
Stats stats() noexcept{return {calls.load(),validations.load(),mismatches.load(),input_bytes.load(),native_bytes.load(),candidate_bytes.load(),native_ns.load(),candidate_ns.load()};}
}
