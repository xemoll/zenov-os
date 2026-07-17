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

#include "../tools/zenpkg/sha256.hpp"

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using int32_t = std::int32_t;
using uintptr_t = std::uintptr_t;
using size_t = std::size_t;

size_t string_length(const char* text) { return text ? std::strlen(text) : 0; }
int string_compare(const char* left, const char* right) { return std::strcmp(left, right); }
bool string_equal(const char* left, const char* right) { return std::strcmp(left, right) == 0; }
bool string_starts_with(const char* text, const char* prefix) { return std::strncmp(text, prefix, std::strlen(prefix)) == 0; }
void string_copy(char* output, const char* input, size_t capacity) { if (!capacity) return; size_t i=0; while(input[i]&&i+1U<capacity){output[i]=input[i];++i;} output[i]=0; }
bool is_space(char value) { return value == ' ' || value == '\t'; }
char ascii_lower(char value) { return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : value; }
void lower_in_place(char* text) { while (*text) { *text = ascii_lower(*text); ++text; } }

namespace console {
std::string output;
void put(char value) { output.push_back(value); }
void write(const char* text) { output += text; }
void line(const char* text) { output += text; output.push_back('\n'); }
void warning(const char* text) { output += "WARN:"; line(text); }
void success(const char* text) { output += "OK:"; line(text); }
void unsigned_dec(uint32_t value) { output += std::to_string(value); }
}
namespace serial {
std::string output;
void put(char value) { output.push_back(value); }
void write(const char* text) { output += text; }
void line(const char* text) { output += text; output.push_back('\n'); }
}
namespace zenov_generated { constexpr uint32_t kShellLineCapacity = 512U; }
namespace heap {
std::vector<uint8_t> arena(512U * 1024U);
uint32_t cursor = 0;
void* allocate(uint32_t size, uint32_t alignment) {
    cursor = (cursor + alignment - 1U) & ~(alignment - 1U);
    if (cursor + size > arena.size()) return nullptr;
    void* result = arena.data() + cursor; cursor += size; return result;
}
}

namespace storage {
constexpr uint8_t zfs_type_file = 1U, zfs_type_directory = 2U;
struct FileInfo { uint32_t type, size, checksum; };
struct Node { uint8_t type; std::vector<uint8_t> data; };
std::map<std::string, Node> nodes;
bool fail_state_write = false, fail_sync = false;
uint32_t fnv1a(const uint8_t* data, uint32_t size) { uint32_t h=2166136261U; for(uint32_t i=0;i<size;++i){h^=data[i];h*=16777619U;} return h; }
bool bytes_equal(const char* left, const char* right, uint32_t count) { return std::memcmp(left,right,count)==0; }
bool bytes_equal(const uint8_t* left, const uint8_t* right, uint32_t count) { uint8_t d=0; for(uint32_t i=0;i<count;++i)d=static_cast<uint8_t>(d|(left[i]^right[i])); return d==0; }
bool ready() { return true; }
uint32_t file_capacity() { return 64U * 1024U; }
std::string key(const char* path) {
    if (!path) return {};
    std::string value(path);
    if (value.rfind("/data",0)==0) value.erase(0,5);
    if (value.empty()) value="/";
    return value;
}
bool normalize_path(const char* input, char output[48]) {
    const std::string value=key(input); if(value.empty()||value[0]!='/'||value.size()>=48U) return false;
    std::memcpy(output,value.c_str(),value.size()+1U); return true;
}
bool query(const char* path, FileInfo& info) {
    auto it=nodes.find(key(path)); if(it==nodes.end()) return false;
    info.type=it->second.type; info.size=static_cast<uint32_t>(it->second.data.size()); info.checksum=fnv1a(it->second.data.data(),info.size); return true;
}
bool make_directory(const char* path) { const auto p=key(path); if(nodes.count(p)) return false; nodes[p]=Node{zfs_type_directory,{}}; return true; }
bool read_file(const char* path,uint8_t* output,uint32_t capacity,uint32_t& actual) {
    auto it=nodes.find(key(path)); if(it==nodes.end()||it->second.type!=zfs_type_file||it->second.data.size()>capacity) return false;
    actual=static_cast<uint32_t>(it->second.data.size()); if(actual) std::memcpy(output,it->second.data.data(),actual); return true;
}
bool write_file(const char* path,const uint8_t* data,uint32_t size,bool append) {
    const auto p=key(path); if(p=="/var/lib/zenpkg/state.v1"&&fail_state_write) return false;
    std::vector<uint8_t> next; if(append&&nodes.count(p)) next=nodes[p].data; if(size) next.insert(next.end(),data,data+size); nodes[p]=Node{zfs_type_file,std::move(next)}; return true;
}
bool remove(const char* path) { return nodes.erase(key(path))>0; }
bool sync_metadata() { return !fail_sync; }
}

namespace process {
std::array<uint8_t,64U*1024U> app_buffer_storage{};
uint8_t* application_buffer=app_buffer_storage.data();
std::string last_run;
void serial_u32(uint32_t value) { serial::write(std::to_string(value).c_str()); }
bool run(char* line) { last_run=line; serial::line("MOCK_APP_RUN_OK"); return true; }
}

namespace security_guard {
constexpr uint32_t sha256_bytes=32U;
enum class Verdict : uint8_t { clean, trusted, untrusted, suspicious, infected, error };
enum class AuditAction : uint8_t { boot, scan, execute, quarantine };
struct ScanResult { Verdict verdict=Verdict::error; char path[48]{}; char signature[48]{}; uint8_t digest[32]{}; uint32_t size=0; bool executable=false; };
uint32_t audit_exec=0;
struct Sha256Context { zenpkg::Sha256 value; };
void sha256_init(Sha256Context& context) { context = Sha256Context{}; }
void sha256_update(Sha256Context& context, const uint8_t* data, uint32_t size) { context.value.update(data,size); }
void sha256_final(Sha256Context& context, uint8_t output[32]) { const auto digest=context.value.final(); std::copy(digest.begin(),digest.end(),output); }
void sha256(const uint8_t* data,uint32_t size,uint8_t output[32]) { zenpkg::Sha256 context; context.update(data,size); const auto digest=context.final(); std::copy(digest.begin(),digest.end(),output); }
bool digest_equal(const uint8_t* left,const uint8_t* right) { uint8_t d=0; for(uint32_t i=0;i<32;++i)d=static_cast<uint8_t>(d|(left[i]^right[i])); return d==0; }
bool zex_valid(const uint8_t* data,uint32_t size) { return data&&size>=32U&&data[0]=='Z'&&data[1]=='E'&&data[2]=='X'&&data[3]=='1'; }
bool elf_valid(const uint8_t* data,uint32_t size) { return data&&size>=4U&&data[0]==0x7f&&data[1]=='E'&&data[2]=='L'&&data[3]=='F'; }
ScanResult scan_memory(const char* path,const uint8_t* data,uint32_t size,bool=false) {
    ScanResult result{}; result.size=size; string_copy(result.path,path?path:"<memory>",sizeof(result.path)); sha256(data,size,result.digest);
    const bool zex=size>=4U&&data[0]=='Z'&&data[1]=='E'&&data[2]=='X'&&data[3]=='1'; const bool elf=size>=4U&&data[0]==0x7f&&data[1]=='E'&&data[2]=='L'&&data[3]=='F';
    result.executable=zex||elf; result.verdict=((zex&&!zex_valid(data,size))||(elf&&!elf_valid(data,size)))?Verdict::suspicious:Verdict::clean; return result;
}
void record(AuditAction action,const ScanResult&) { if(action==AuditAction::execute)++audit_exec; }
}

namespace zgdb {
struct BigInteger { uint32_t limb[64]; };
void big_zero(BigInteger& value){std::memset(value.limb,0,sizeof(value.limb));}
void big_from_be(BigInteger& value,const uint8_t input[256]){big_zero(value);for(uint32_t i=0;i<256U;++i){const uint32_t reverse=255U-i;value.limb[reverse/4U]|=static_cast<uint32_t>(input[i])<<((reverse%4U)*8U);}}
void big_to_be(const BigInteger& value,uint8_t output[256]){for(uint32_t i=0;i<256U;++i){const uint32_t reverse=255U-i;output[i]=static_cast<uint8_t>(value.limb[reverse/4U]>>((reverse%4U)*8U));}}
int32_t big_compare(const BigInteger& left,const BigInteger& right){for(uint32_t i=64U;i>0;--i){const uint32_t n=i-1U;if(left.limb[n]<right.limb[n])return -1;if(left.limb[n]>right.limb[n])return 1;}return 0;}
void big_subtract(BigInteger& left,const BigInteger& right){unsigned long long borrow=0;for(uint32_t i=0;i<64U;++i){const unsigned long long current=left.limb[i],sub=static_cast<unsigned long long>(right.limb[i])+borrow;left.limb[i]=static_cast<uint32_t>(current-sub);borrow=current<sub?1ULL:0ULL;}}
void big_add_plain(BigInteger& output,const BigInteger& left,const BigInteger& right){unsigned long long carry=0;for(uint32_t i=0;i<64U;++i){const unsigned long long sum=static_cast<unsigned long long>(left.limb[i])+right.limb[i]+carry;output.limb[i]=static_cast<uint32_t>(sum);carry=sum>>32U;}}
void big_add_mod(BigInteger& output,const BigInteger& left,const BigInteger& right,const BigInteger& modulus){BigInteger threshold=modulus;big_subtract(threshold,right);if(big_compare(left,threshold)>=0){output=left;big_subtract(output,threshold);}else big_add_plain(output,left,right);}
bool big_bit(const BigInteger& value,uint32_t bit){return((value.limb[bit/32U]>>(bit%32U))&1U)!=0;}
void big_multiply_mod(BigInteger& output,const BigInteger& left,const BigInteger& right,const BigInteger& modulus){BigInteger result{},current=left,temp{};for(uint32_t bit=0;bit<2048U;++bit){if(big_bit(right,bit)){big_add_mod(temp,result,current,modulus);result=temp;}big_add_mod(temp,current,current,modulus);current=temp;}output=result;}
void big_exp_65537(BigInteger& output,const BigInteger& signature,const BigInteger& modulus){BigInteger result{},temp{};result.limb[0]=1U;constexpr uint32_t exponent=65537U;for(int32_t bit=16;bit>=0;--bit){big_multiply_mod(temp,result,result,modulus);result=temp;if((exponent>>static_cast<uint32_t>(bit))&1U){big_multiply_mod(temp,result,signature,modulus);result=temp;}}output=result;}
}

#include "../kernel/parts/package_format.inc"
#include "../kernel/parts/package_catalog.inc"
#include "../kernel/parts/package_manager.inc"

std::vector<uint8_t> read_file(const std::filesystem::path& path) { return zenpkg::read_binary(path); }
void put(const std::string& path,const std::vector<uint8_t>& data) { storage::nodes[storage::key(path.c_str())]={storage::zfs_type_file,data}; }
void reset_runtime() { package_manager::database={}; package_manager::package_buffer=nullptr; package_manager::verification_buffer=nullptr; package_manager::initialized=false; package_catalog::ready=false; package_catalog::active_record_count=0; package_catalog::active_catalog_version=0; package_catalog::persistent_catalog_version=0; heap::cursor=0; serial::output.clear(); console::output.clear(); process::last_run.clear(); }

int main(int argc,char** argv) {
    assert(argc==2); const std::filesystem::path fixture=argv[1];
    storage::nodes["/security"]={storage::zfs_type_directory,{}}; storage::nodes["/security/updates"]={storage::zfs_type_directory,{}}; storage::nodes["/packages"]={storage::zfs_type_directory,{}}; storage::nodes["/apps"]={storage::zfs_type_directory,{}};
    put("/security/zenpkg.zpc",read_file(fixture/"zenpkg-v1.zpc")); put("/security/zenpkg.version",{'1','\n'});
    put("/security/updates/zenpkg-v2.zpc",read_file(fixture/"zenpkg-v2.zpc")); put("/security/updates/zenpkg-tampered.zpc",read_file(fixture/"zenpkg-tampered.zpc"));
    put("/packages/hello-native-0.1.0.zpk",read_file(fixture/"hello-native-0.1.0.zpk")); put("/packages/hello-native-0.2.0.zpk",read_file(fixture/"hello-native-0.2.0.zpk"));
    assert(package_catalog::init()); package_manager::init(); assert(package_manager::initialized);
    assert(package_manager::verify("/packages/hello-native-0.1.0.zpk")); assert(package_manager::install("/packages/hello-native-0.1.0.zpk"));
    assert(!package_manager::install("/packages/hello-native-0.2.0.zpk")); assert(serial::output.find("ZENPKG_CATALOG_REJECTED")!=std::string::npos);
    assert(!package_manager::update_catalog("/security/updates/zenpkg-tampered.zpc"));
    assert(package_manager::update_catalog("/security/updates/zenpkg-v2.zpc")); assert(package_catalog::active_catalog_version==2U);
    assert(package_manager::install("/packages/hello-native-0.2.0.zpk")); assert(std::string(package_manager::database.records[0].active.version)=="0.2.0");
    assert(package_manager::rollback("hello-native")); assert(std::string(package_manager::database.records[0].active.version)=="0.1.0");
    char run[]="hello-native alpha"; assert(package_manager::run_package(run));
    const auto& payload=storage::nodes["/apps/pkg-hello-native-0.1.0.zex"].data; assert(package_manager::trusted_execution("/data/apps/pkg-hello-native-0.1.0.zex",payload.data(),static_cast<uint32_t>(payload.size())));
    auto altered=payload; altered.back()^=1U; assert(!package_manager::trusted_execution("/data/apps/pkg-hello-native-0.1.0.zex",altered.data(),static_cast<uint32_t>(altered.size())));
    const uint32_t generation=package_manager::database.generation; reset_runtime(); assert(package_catalog::init()); package_manager::init(); assert(package_manager::initialized&&package_manager::database.generation==generation);
    storage::fail_state_write=true; assert(package_manager::rollback("hello-native")==false); storage::fail_state_write=false;
    assert(package_manager::remove_package("hello-native")); reset_runtime(); assert(package_catalog::init()); package_manager::init(); assert(package_manager::find_record("hello-native")==-1);
    auto saved=storage::nodes["/var/lib/zenpkg/state.v1"].data; storage::nodes["/var/lib/zenpkg/state.v1"].data[0]^=0xff; reset_runtime(); assert(package_catalog::init()); package_manager::init(); assert(!package_manager::initialized); storage::nodes["/var/lib/zenpkg/state.v1"].data=saved;
    std::cout<<"ZENPKG_SIGNED_HOST_TEST_OK\n"; return 0;
}
