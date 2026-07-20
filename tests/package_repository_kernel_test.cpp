#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using int32_t = std::int32_t;
using size_t = std::size_t;
using std::memcpy;
using std::memset;

bool string_equal(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
bool string_starts_with(const char* text, const char* prefix) {
    return text && prefix && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

namespace serial {
std::string output;
void write(const char* text) { output += text ? text : "<null>"; }
void line(const char* text) { write(text); output += '\n'; }
}
namespace console {
std::string output;
void write(const char* text) { output += text ? text : "<null>"; }
void line(const char* text) { write(text); output += '\n'; }
void put(char c) { output.push_back(c); }
void unsigned_dec(uint32_t value) { output += std::to_string(value); }
}
namespace heap {
std::array<uint8_t, 64U * 1024U> arena{};
uint32_t cursor = 0;
void* allocate(uint32_t size, uint32_t alignment) {
    cursor = (cursor + alignment - 1U) & ~(alignment - 1U);
    if (cursor + size > arena.size()) return nullptr;
    void* result = arena.data() + cursor;
    cursor += size;
    return result;
}
}

namespace security_guard {
constexpr uint32_t sha256_bytes = 32U;
struct Sha256Context {
    uint32_t state[8]{};
    uint8_t block[64]{};
    unsigned long long total = 0;
    uint32_t used = 0;
};
static constexpr uint32_t k[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};
uint32_t rr(uint32_t value, uint32_t count) { return (value >> count) | (value << (32U - count)); }
void transform(Sha256Context& context, const uint8_t* block) {
    uint32_t words[64]{};
    for (uint32_t i = 0; i < 16U; ++i) words[i] =
        (static_cast<uint32_t>(block[i*4]) << 24U) | (static_cast<uint32_t>(block[i*4+1]) << 16U) |
        (static_cast<uint32_t>(block[i*4+2]) << 8U) | block[i*4+3];
    for (uint32_t i = 16U; i < 64U; ++i) {
        const uint32_t s0 = rr(words[i-15],7)^rr(words[i-15],18)^(words[i-15]>>3U);
        const uint32_t s1 = rr(words[i-2],17)^rr(words[i-2],19)^(words[i-2]>>10U);
        words[i] = words[i-16] + s0 + words[i-7] + s1;
    }
    uint32_t a=context.state[0],b=context.state[1],c=context.state[2],d=context.state[3];
    uint32_t e=context.state[4],f=context.state[5],g=context.state[6],h=context.state[7];
    for (uint32_t i=0;i<64U;++i) {
        const uint32_t s1=rr(e,6)^rr(e,11)^rr(e,25), choose=(e&f)^((~e)&g);
        const uint32_t t1=h+s1+choose+k[i]+words[i], s0=rr(a,2)^rr(a,13)^rr(a,22);
        const uint32_t t2=s0+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    context.state[0]+=a;context.state[1]+=b;context.state[2]+=c;context.state[3]+=d;
    context.state[4]+=e;context.state[5]+=f;context.state[6]+=g;context.state[7]+=h;
}
void sha256_init(Sha256Context& context) {
    context = Sha256Context{};
    const uint32_t initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memcpy(context.state, initial, sizeof(initial));
}
void sha256_update(Sha256Context& context, const uint8_t* data, uint32_t size) {
    context.total += size;
    while (size) {
        uint32_t chunk = 64U - context.used; if (chunk > size) chunk = size;
        memcpy(context.block + context.used, data, chunk); context.used += chunk; data += chunk; size -= chunk;
        if (context.used == 64U) { transform(context, context.block); context.used = 0; }
    }
}
void sha256_final(Sha256Context& context, uint8_t output[32]) {
    const unsigned long long bits = context.total * 8ULL;
    context.block[context.used++] = 0x80U;
    if (context.used > 56U) { while (context.used < 64U) context.block[context.used++] = 0; transform(context, context.block); context.used = 0; }
    while (context.used < 56U) context.block[context.used++] = 0;
    for (int shift=56; shift>=0; shift-=8) context.block[context.used++] = static_cast<uint8_t>(bits >> shift);
    transform(context, context.block);
    for (uint32_t i=0;i<8U;++i) { output[i*4]=context.state[i]>>24U; output[i*4+1]=context.state[i]>>16U; output[i*4+2]=context.state[i]>>8U; output[i*4+3]=context.state[i]; }
}
void sha256(const uint8_t* data, uint32_t size, uint8_t output[32]) { Sha256Context c{}; sha256_init(c); sha256_update(c,data,size); sha256_final(c,output); }
bool digest_equal(const uint8_t* a, const uint8_t* b) { uint8_t d=0; for(uint32_t i=0;i<32U;++i)d=static_cast<uint8_t>(d|(a[i]^b[i])); return d==0; }
}

namespace storage {
constexpr uint8_t zfs_type_file = 1U;
struct FileInfo { uint32_t type=0,size=0,checksum=0; };
std::map<std::string,std::vector<uint8_t>> files;
bool online = true;
uint32_t fnv1a(const uint8_t* data, uint32_t size) { uint32_t h=2166136261U; for(uint32_t i=0;i<size;++i){h^=data[i];h*=16777619U;} return h; }
bool bytes_equal(const char* a,const char* b,uint32_t n){return std::memcmp(a,b,n)==0;}
bool bytes_equal(const uint8_t* a,const uint8_t* b,uint32_t n){return std::memcmp(a,b,n)==0;}
bool ready(){return online;}
bool query(const char* path, FileInfo& info){auto it=files.find(path);if(it==files.end())return false;info.type=zfs_type_file;info.size=static_cast<uint32_t>(it->second.size());info.checksum=fnv1a(it->second.data(),info.size);return true;}
bool read_file(const char* path,uint8_t* out,uint32_t capacity,uint32_t& actual){auto it=files.find(path);if(it==files.end()||it->second.size()>capacity)return false;actual=static_cast<uint32_t>(it->second.size());if(actual)memcpy(out,it->second.data(),actual);return true;}
bool package_write_file(const char* path,const uint8_t* data,uint32_t size,bool append){if(append)return false;files[path]=std::vector<uint8_t>(data,data+size);return true;}
bool sync_metadata(){return true;}
}
namespace process {
constexpr uint32_t capability_console_write=1U<<0U, capability_ticks=1U<<1U, capability_file_read=1U<<2U,
    capability_file_write=1U<<3U, capability_file_stat=1U<<4U, capability_version=1U<<5U,
    capability_sync=1U<<6U, capability_console_read=1U<<7U;
constexpr uint32_t capability_all=capability_console_write|capability_ticks|capability_file_read|capability_file_write|
    capability_file_stat|capability_version|capability_sync|capability_console_read;
void serial_u32(uint32_t value){serial::write(std::to_string(value).c_str());}
}

#include "../kernel/parts/rsa_pss.inc"
#include "../kernel/parts/package_repository.inc"

std::vector<uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input); input.seekg(0,std::ios::end); const auto size=input.tellg(); assert(size>=0);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size)); input.seekg(0,std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(input || bytes.empty());
    return bytes;
}
void load(const std::filesystem::path& directory,const std::string& source,const std::string& destination){storage::files["/repo/"+destination]=read_all(directory/source);}
void load_current(const std::filesystem::path& directory){
    storage::files.clear();
    load(directory,"root-bootstrap.zrm","root-bootstrap.zrm"); load(directory,"root.zrm","root.zrm");
    load(directory,"targets.zrm","targets.zrm"); load(directory,"native-apps.zrm","native-apps.zrm");
    load(directory,"snapshot.zrm","snapshot.zrm"); load(directory,"timestamp.zrm","timestamp.zrm");
}
void reset_runtime(){
    heap::cursor=0; serial::output.clear(); console::output.clear();
    package_repository::metadata_buffer=nullptr; package_repository::active_root={};
    memset(package_repository::active_targets,0,sizeof(package_repository::active_targets));
    package_repository::active_target_count=0; package_repository::persistent_state={}; package_repository::ready=false;
}

int main(int argc,char** argv){
    if(argc!=2){std::cerr<<"usage: package-repository-kernel-test <metadata-dir>\n";return 2;}
    const std::filesystem::path meta=argv[1];
    load_current(meta); reset_runtime();
    assert(package_repository::init()); assert(package_repository::ready); assert(package_repository::target_count()==2U);
    const auto* v1=package_repository::find_target("hello-native","0.1.0","/data/apps/pkg-hello-native-0.1.0.zex",1U);
    assert(v1 && v1->syscall_mask==process::capability_console_write && v1->read_scope[0]==0 && v1->write_scope[0]==0);
    assert(storage::files.count(package_repository::state_path)==1U);
    const auto current_state=storage::files[package_repository::state_path];

    load(meta,"expired.timestamp.zrm","timestamp.zrm"); assert(!package_repository::refresh()); assert(package_repository::ready);
    load(meta,"timestamp.zrm","timestamp.zrm"); assert(package_repository::refresh());

    load(meta,"mixmatch.snapshot.zrm","snapshot.zrm"); load(meta,"mixmatch.timestamp.zrm","timestamp.zrm");
    assert(!package_repository::refresh());
    load(meta,"snapshot.zrm","snapshot.zrm"); load(meta,"timestamp.zrm","timestamp.zrm"); assert(package_repository::refresh());

    load(meta,"bad-signature.native-apps.zrm","native-apps.zrm"); load(meta,"bad-signature.snapshot.zrm","snapshot.zrm");
    load(meta,"bad-signature.timestamp.zrm","timestamp.zrm"); assert(!package_repository::refresh());
    load(meta,"native-apps.zrm","native-apps.zrm"); load(meta,"snapshot.zrm","snapshot.zrm"); load(meta,"timestamp.zrm","timestamp.zrm"); assert(package_repository::refresh());

    load(meta,"rollback.snapshot.zrm","snapshot.zrm"); load(meta,"rollback.timestamp.zrm","timestamp.zrm"); assert(!package_repository::refresh());

    load_current(meta); storage::files[package_repository::state_path]=current_state; storage::files[package_repository::state_path][0]^=0xFFU;
    reset_runtime(); assert(!package_repository::init()); assert(!package_repository::ready);
    assert(serial::output.find("ZENREPO_STATE_INVALID")!=std::string::npos);
    std::cout<<"PACKAGE_REPOSITORY_KERNEL_TEST_OK root-rotation delegation expiry mixmatch signature rollback corrupt-state\n";
}
