#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using int32_t = std::int32_t;
using size_t = std::size_t;

size_t string_length(const char* text) { return text ? std::strlen(text) : 0; }
int string_compare(const char* a, const char* b) { return std::strcmp(a, b); }
bool string_equal(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
bool string_starts_with(const char* text, const char* prefix) { return std::strncmp(text, prefix, std::strlen(prefix)) == 0; }
bool is_space(char c) { return c == ' ' || c == '\t'; }
char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }
void lower_in_place(char* text) { while (*text) { *text = ascii_lower(*text); ++text; } }

namespace console {
std::string output;
void put(char c) { output.push_back(c); }
void write(const char* text) { output += text; }
void line(const char* text) { output += text; output += '\n'; }
void warning(const char* text) { output += "WARN:"; line(text); }
void success(const char* text) { output += "OK:"; line(text); }
void unsigned_dec(uint32_t value) { output += std::to_string(value); }
}
namespace serial {
std::string output;
void put(char c) { output.push_back(c); }
void write(const char* text) { output += text; }
void line(const char* text) { output += text; output += '\n'; }
}
namespace zenov_generated { constexpr uint32_t kShellLineCapacity = 512U; constexpr const char* kVersion = "0.1.1"; }
namespace heap {
std::vector<uint8_t> arena(128U * 1024U);
uint32_t cursor = 0;
void* allocate(uint32_t size, uint32_t alignment) {
    cursor = (cursor + alignment - 1U) & ~(alignment - 1U);
    if (cursor + size > arena.size()) return nullptr;
    void* result = arena.data() + cursor;
    cursor += size;
    return result;
}
}

#include "../kernel/parts/crypto_sha256.inc"

namespace storage {
constexpr uint8_t zfs_type_file = 1U;
constexpr uint8_t zfs_type_directory = 2U;
struct FileInfo { uint32_t type, size, checksum; };
struct Node { uint8_t type; std::vector<uint8_t> data; };
std::map<std::string, Node> nodes;
bool fail_state_write = false;
bool fail_sync = false;
uint32_t generation = 1;
uint32_t fnv1a(const uint8_t* data, uint32_t size) { uint32_t h=2166136261U; for(uint32_t i=0;i<size;++i){h^=data[i];h*=16777619U;} return h; }
bool bytes_equal(const char* left, const char* right, uint32_t count) { return std::memcmp(left,right,count)==0; }
bool ready() { return true; }
uint32_t file_capacity() { return 64U * 1024U; }
bool query(const char* path, FileInfo& info) {
    auto it=nodes.find(path); if(it==nodes.end()) return false;
    info.type=it->second.type; info.size=static_cast<uint32_t>(it->second.data.size()); info.checksum=fnv1a(it->second.data.data(),info.size); return true;
}
bool make_directory(const char* path) { if(nodes.count(path)) return false; nodes[path]=Node{zfs_type_directory,{}}; return true; }
bool read_file(const char* path, uint8_t* out, uint32_t capacity, uint32_t& actual) {
    auto it=nodes.find(path); if(it==nodes.end()||it->second.type!=zfs_type_file||it->second.data.size()>capacity) return false;
    actual=static_cast<uint32_t>(it->second.data.size()); if(actual) std::memcpy(out,it->second.data.data(),actual); return true;
}
bool write_file(const char* path,const uint8_t* data,uint32_t size,bool append) {
    if(std::string(path)=="/var/lib/zenpkg/state.v1" && fail_state_write) return false;
    std::vector<uint8_t> next;
    if(append && nodes.count(path)) next=nodes[path].data;
    if(size) next.insert(next.end(),data,data+size);
    nodes[path]=Node{zfs_type_file,std::move(next)}; return true;
}
bool sync_metadata() { if(fail_sync) return false; ++generation; return true; }
bool remove(const char* path) { return nodes.erase(path)>0; }
bool package_write_file(const char* path,const uint8_t* data,uint32_t size,bool append) { return write_file(path,data,size,append); }
bool package_remove(const char* path) { return remove(path); }
bool normalize_path(const char* input, char output[48]) {
    if (!input || !*input) return false;
    const char* source = input;
    if (std::strncmp(source, "/data", 5) == 0) source += 5;
    if (!*source) source = "/";
    if (*source != '/') return false;
    if (std::strlen(source) >= 48U) return false;
    std::strcpy(output, source); return true;
}
}

namespace security_guard {
constexpr uint32_t sha256_bytes = 32U;
enum class Verdict : uint8_t { clean, trusted, untrusted, suspicious, infected, error };
enum class AuditAction : uint8_t { boot, scan, execute, quarantine };
struct ScanResult { Verdict verdict = Verdict::error; char path[48]{}; char signature[48]{}; uint8_t digest[32]{}; uint32_t size = 0; bool executable = false; };
uint32_t execution_records = 0;
ScanResult scan_memory(const char* path, const uint8_t* data, uint32_t size, bool = true) {
    ScanResult result{}; result.verdict = Verdict::clean; result.size = size;
    if (path) { const std::size_t n = std::min<std::size_t>(std::strlen(path), sizeof(result.path) - 1U); std::memcpy(result.path, path, n); result.path[n] = 0; }
    crypto::sha256(data, size, result.digest);
    return result;
}
bool digest_equal(const uint8_t* left, const uint8_t* right) { return crypto::digest_equal(left, right); }
bool record(AuditAction action, const ScanResult&) { if (action == AuditAction::execute) ++execution_records; return true; }
}

namespace process {
std::string last_run;
void serial_u32(uint32_t value) { serial::write(std::to_string(value).c_str()); }
bool run(char* line) { last_run=line; serial::line("MOCK_APP_RUN_OK"); return true; }
}

#include "../kernel/parts/package_format.inc"
#include "../kernel/parts/package_manager.inc"

static void append_u32(std::vector<uint8_t>& out,uint32_t v){for(int s=0;s<32;s+=8)out.push_back(static_cast<uint8_t>(v>>s));}
static void append_u64(std::vector<uint8_t>& out,unsigned long long v){for(int s=0;s<64;s+=8)out.push_back(static_cast<uint8_t>(v>>s));}
static std::vector<uint8_t> package_bytes(const std::string& version, const std::vector<uint8_t>& payload, const std::vector<std::string>& extras={}) {
    std::string entry="/data/apps/pkg-hello-native-"+version+".zex";
    std::string manifest=
        "format=zenpkg-manifest-1\n"
        "name=hello-native\n"
        "version="+version+"\n"
        "architecture=i686\n"
        "target=i686-zenov-none\n"
        "kind=application\n"
        "entrypoint="+entry+"\n"
        "payload_type=zex1\n"
        "runtime=native\n"
        "min_os=0.1.1\n"
        "license=BSD-2-Clause\n"
        "source=https://github.com/xemoll/zenov-os\n"
        "asset_policy=redistributable\n"
        "capability=abi.zex1.v1\n"
        "capability=kernel.ring3\n"
        "capability=storage.zenovfs1\n";
    for(const auto& e:extras) manifest += e + "\n";
    uint8_t md[32],pd[32]; crypto::sha256(reinterpret_cast<const uint8_t*>(manifest.data()),manifest.size(),md); crypto::sha256(payload.data(),payload.size(),pd);
    std::vector<uint8_t> out={'Z','E','N','P','K','G','1',0}; append_u32(out,1); append_u32(out,0); append_u64(out,manifest.size()); append_u64(out,payload.size());
    out.insert(out.end(),md,md+32); out.insert(out.end(),pd,pd+32); uint8_t hd[32]; crypto::sha256(out.data(),out.size(),hd); out.insert(out.end(),hd,hd+32);
    out.insert(out.end(),manifest.begin(),manifest.end()); out.insert(out.end(),payload.begin(),payload.end()); return out;
}
static void put_file(const std::string& path,const std::vector<uint8_t>& bytes){storage::nodes[path]={storage::zfs_type_file,bytes};}
static void reset_runtime_only(){ package_manager::database={}; package_manager::package_buffer=nullptr; package_manager::initialized=false; heap::cursor=0; console::output.clear(); serial::output.clear(); process::last_run.clear(); }

int main(){
    const std::vector<uint8_t> zex={
        0x5a,0x45,0x58,0x31,0x01,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x6e,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x16,0x9a,0x8d,0x09,
        0xb8,0x01,0x00,0x00,0x00,0xbb,0x20,0x00,0x00,0x00,0xb9,0x4e,0x00,0x00,0x00,0xcd,
        0x80,0x31,0xdb,0x31,0xc0,0xcd,0x80,0x0f,0x0b,0xeb,0xfc,0x00,0x00,0x00,0x00,0x00,
        0x48,0x45,0x4c,0x4c,0x4f,0x5f,0x5a,0x45,0x58,0x5f,0x30,0x5f,0x31,0x5f,0x31,0x5f,
        0x4f,0x4b,0x0a,0x5a,0x65,0x6e,0x6f,0x76,0x4f,0x53,0x20,0x6e,0x61,0x74,0x69,0x76,
        0x65,0x20,0x5a,0x45,0x58,0x31,0x20,0x61,0x70,0x70,0x6c,0x69,0x63,0x61,0x74,0x69,
        0x6f,0x6e,0x20,0x72,0x65,0x74,0x75,0x72,0x6e,0x65,0x64,0x20,0x74,0x68,0x72,0x6f,
        0x75,0x67,0x68,0x20,0x49,0x4e,0x54,0x20,0x30,0x78,0x38,0x30,0x2e,0x0a
    };
    auto v1=package_bytes("0.1.0",zex); auto v2=package_bytes("0.2.0",zex); auto v3=package_bytes("0.3.0",zex);
    put_file("/packages/hello-native-0.1.0.zpk",v1); put_file("/packages/hello-native-0.2.0.zpk",v2); put_file("/packages/hello-native-0.3.0.zpk",v3);
    auto dependent=package_bytes("0.1.5",zex,{"dependency=missing-runtime@1.0.0"});
    put_file("/packages/hello-native-0.1.5.zpk",dependent);
    package_manager::init(); assert(package_manager::initialized); assert(serial::output.find("ZENPKG_MANAGER_READY")!=std::string::npos);
    native_package::VerifiedPackage dependency_package{};
    assert(native_package::verify_bytes(dependent.data(), static_cast<uint32_t>(dependent.size()), dependency_package)==native_package::Error::none);
    const char* requirement=nullptr; assert(!package_manager::requirements_satisfied(dependency_package.manifest, requirement)); assert(requirement);
    assert(!package_manager::install("/packages/hello-native-0.1.5.zpk")); assert(package_manager::database.count==0);
    assert(package_manager::verify("/packages/hello-native-0.3.0.zpk")); assert(!package_manager::install("/packages/hello-native-0.3.0.zpk"));
    assert(serial::output.find("ZENPKG_UNAUTHORIZED_PACKAGE")!=std::string::npos);
    assert(package_manager::install("/packages/hello-native-0.1.0.zpk")); assert(package_manager::database.count==1); assert(std::string(package_manager::database.records[0].active.version)=="0.1.0");
    assert(package_manager::install("/packages/hello-native-0.1.0.zpk"));
    auto conflicting=package_bytes("0.1.0",std::vector<uint8_t>{'Z','E','X','1',9,9,9,9});
    put_file("/packages/hello-native-conflict.zpk",conflicting); assert(!package_manager::install("/packages/hello-native-conflict.zpk"));
    assert(package_manager::install("/packages/hello-native-0.2.0.zpk")); assert(std::string(package_manager::database.records[0].active.version)=="0.2.0"); assert(std::string(package_manager::database.records[0].previous.version)=="0.1.0");
    assert(!package_manager::install("/packages/hello-native-0.1.0.zpk")); assert(serial::output.find("ZENPKG_DOWNGRADE_REJECTED")!=std::string::npos);
    assert(package_manager::rollback("hello-native")); assert(std::string(package_manager::database.records[0].active.version)=="0.1.0");
    char run[]="hello-native alpha"; assert(package_manager::run_package(run)); assert(process::last_run=="/data/apps/pkg-hello-native-0.1.0.zex alpha");
    const auto& installed_payload=storage::nodes["/data/apps/pkg-hello-native-0.1.0.zex"].data;
    assert(package_manager::allow_execution("/data/apps/pkg-hello-native-0.1.0.zex", installed_payload.data(), static_cast<uint32_t>(installed_payload.size())));
    auto altered_payload=installed_payload; altered_payload.back()^=1U;
    assert(!package_manager::allow_execution("/data/apps/pkg-hello-native-0.1.0.zex", altered_payload.data(), static_cast<uint32_t>(altered_payload.size())));
    char dispatched[]="pkg status"; assert(package_manager::dispatch_line(dispatched));
    char unrelated[]="status"; assert(!package_manager::dispatch_line(unrelated));
    uint32_t committed_generation=package_manager::database.generation;
    reset_runtime_only(); package_manager::init(); assert(package_manager::database.generation==committed_generation); assert(package_manager::find_record("hello-native")>=0);
    const auto authentic_state=storage::nodes[package_manager::state_path].data;
    package_manager::Database forged=package_manager::database;
    forged.records[0].active.payload_digest[0]^=0x80U;
    forged.checksum=package_manager::database_checksum(forged);
    storage::nodes[package_manager::state_path].data.assign(reinterpret_cast<const uint8_t*>(&forged), reinterpret_cast<const uint8_t*>(&forged)+sizeof(forged));
    reset_runtime_only(); package_manager::init(); assert(!package_manager::initialized); assert(serial::output.find("ZENPKG_ACTIVE_PAYLOAD_INVALID")!=std::string::npos);
    storage::nodes[package_manager::state_path].data=authentic_state;
    reset_runtime_only(); package_manager::init(); assert(package_manager::initialized);
    storage::fail_state_write=true; assert(!package_manager::install("/packages/hello-native-0.2.0.zpk")); storage::fail_state_write=false; assert(std::string(package_manager::database.records[0].active.version)=="0.1.0"); assert(storage::nodes.count("/data/apps/pkg-hello-native-0.2.0.zex"));
    auto corrupt=v2; corrupt.back()^=0xff; put_file("/packages/corrupt.zpk",corrupt); assert(!package_manager::verify("/packages/corrupt.zpk"));
    assert(package_manager::remove_package("hello-native")); reset_runtime_only(); package_manager::init(); assert(package_manager::find_record("hello-native")==-1);
    auto saved_state=storage::nodes["/var/lib/zenpkg/state.v1"].data;
    storage::nodes["/var/lib/zenpkg/state.v1"].data[0]^=0xff;
    reset_runtime_only(); package_manager::init(); assert(!package_manager::initialized); assert(serial::output.find("ZENPKG_DB_INVALID")!=std::string::npos);
    storage::nodes["/var/lib/zenpkg/state.v1"].data=saved_state;
    std::cout << "PACKAGE_MANAGER_HOST_TEST_OK\n";
}
