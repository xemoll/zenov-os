#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenpkg/sha256.hpp"
#include "../security/zenpkg_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct CatalogHeader {
    char magic[4]; std::uint16_t schema, header_size; std::uint32_t catalog_version, minimum_engine, record_count, payload_size;
    std::uint8_t payload_sha256[32]; std::uint8_t key_id[8];
};
struct CatalogRecord {
    char name[32], version[16], entrypoint[48]; std::uint32_t package_size, payload_size;
    std::uint8_t payload_type, flags; std::uint16_t reserved;
    std::uint8_t package_sha256[32], payload_sha256[32];
};
#pragma pack(pop)
static_assert(sizeof(CatalogHeader) == 64 && sizeof(CatalogRecord) == 172);

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) { return zenpkg::read_binary(path); }
void write_all(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) { zenpkg::write_binary_atomic(path, bytes); }
void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) { for (unsigned shift=0; shift<32; shift+=8) output.push_back(static_cast<std::uint8_t>(value>>shift)); }
void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) { for (unsigned shift=0; shift<64; shift+=8) output.push_back(static_cast<std::uint8_t>(value>>shift)); }

template <std::size_t N> void fixed(char (&output)[N], const std::string& value) {
    if (value.empty() || value.size() >= N) throw std::runtime_error("fixed field overflow");
    std::memset(output,0,N); std::memcpy(output,value.data(),value.size());
}
std::vector<std::uint8_t> manifest(const std::string& version) {
    const std::string text =
        "format=zenpkg-manifest-1\nname=hello-native\nversion="+version+"\narchitecture=i686\n"
        "target=i686-zenov-none\nkind=application\nentrypoint=/data/apps/pkg-hello-native-"+version+".zex\n"
        "payload_type=zex1\nruntime=native\nmin_os=0.1.1\nlicense=MIT\nsource=https://github.com/xemoll/zenov-os\n"
        "asset_policy=redistributable\ncapability=abi.zex1.v1\ncapability=kernel.ring3\ncapability=storage.zenovfs1\n";
    return {text.begin(),text.end()};
}
std::vector<std::uint8_t> package(const std::string& version,const std::vector<std::uint8_t>& payload) {
    const auto m=manifest(version); std::vector<std::uint8_t> out={'Z','E','N','P','K','G','1',0};
    append_u32(out,1); append_u32(out,0); append_u64(out,m.size()); append_u64(out,payload.size());
    const auto md=zenpkg::Sha256::hash(m), pd=zenpkg::Sha256::hash(payload);
    out.insert(out.end(),md.begin(),md.end()); out.insert(out.end(),pd.begin(),pd.end());
    const auto hd=zenpkg::Sha256::hash(out); out.insert(out.end(),hd.begin(),hd.end());
    out.insert(out.end(),m.begin(),m.end()); out.insert(out.end(),payload.begin(),payload.end()); return out;
}
CatalogRecord record(const std::string& version,const std::vector<std::uint8_t>& pkg,const std::vector<std::uint8_t>& payload) {
    CatalogRecord r{}; fixed(r.name,"hello-native"); fixed(r.version,version); fixed(r.entrypoint,"/data/apps/pkg-hello-native-"+version+".zex");
    r.package_size=static_cast<std::uint32_t>(pkg.size()); r.payload_size=static_cast<std::uint32_t>(payload.size()); r.payload_type=1;
    const auto a=zenpkg::Sha256::hash(pkg), b=zenpkg::Sha256::hash(payload); std::copy(a.begin(),a.end(),r.package_sha256); std::copy(b.begin(),b.end(),r.payload_sha256); return r;
}
std::vector<std::uint8_t> catalog(std::uint32_t version,const std::vector<CatalogRecord>& records,const unsigned char signature[256]) {
    CatalogHeader h{{'Z','P','C','2'},2,sizeof(CatalogHeader),version,0x102u,static_cast<std::uint32_t>(records.size()),static_cast<std::uint32_t>(records.size()*sizeof(CatalogRecord)),{}, {}};
    std::vector<std::uint8_t> payload(records.size()*sizeof(CatalogRecord)); std::memcpy(payload.data(),records.data(),payload.size());
    const auto digest=zenpkg::Sha256::hash(payload); std::copy(digest.begin(),digest.end(),h.payload_sha256); std::copy(zenpkg_crypto::kZenpkgRootKeyId,zenpkg_crypto::kZenpkgRootKeyId+8,h.key_id);
    std::vector<std::uint8_t> out(sizeof(h)); std::memcpy(out.data(),&h,sizeof(h)); out.insert(out.end(),payload.begin(),payload.end()); out.insert(out.end(),signature,signature+256); return out;
}
void require(const std::vector<std::uint8_t>& bytes,const char* expected,const char* label) {
    const auto actual=zenpkg::sha256_hex(bytes); if(actual!=expected) throw std::runtime_error(std::string(label)+" hash mismatch: "+actual);
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3){std::cerr<<"usage: zenpkg-fixture-export <HELLO.ZEX> <output-dir>\n";return 2;}
        const auto payload=read_all(argv[1]); require(payload,"2b7ba0114d5228825b30aca30e0e978f2faf9b798cf7f5494742d7a1d330956a","HELLO.ZEX");
        const auto p1=package("0.1.0",payload),p2=package("0.2.0",payload); require(p1,"9dc941c4848c19007e579927bc73803431f38b8e23344adff5c775a25196f490","package v1"); require(p2,"0e0b191098517e0ae9cba1f5b953fb051f2cddc74ad9205df5229876e99044f8","package v2");
        const auto r1=record("0.1.0",p1,payload),r2=record("0.2.0",p2,payload);
        const auto c1=catalog(1,{r1},zenpkg_crypto::kZenpkgCatalogV1Signature),c2=catalog(2,{r1,r2},zenpkg_crypto::kZenpkgCatalogV2Signature);
        auto bad=c2; bad.at(80)^=0x40; require(c1,"62e5817029d6c2598f263a257792327ed671ce80cb68feb6efbf690bb1229c14","catalog v1"); require(c2,"c3471f1afcdb365a92ac93f1ca182508aa03806bce3a9e53001c2675c914f95b","catalog v2"); require(bad,"d6bbbff66a1431843deaf52fb5a24005c110014b8390e14dd2fcee4608665d67","tampered catalog");
        const std::filesystem::path out=argv[2]; std::filesystem::create_directories(out); write_all(out/"hello-native-0.1.0.zpk",p1); write_all(out/"hello-native-0.2.0.zpk",p2); write_all(out/"zenpkg-v1.zpc",c1); write_all(out/"zenpkg-v2.zpc",c2); write_all(out/"zenpkg-tampered.zpc",bad);
        std::cout<<"ZENPKG_SIGNED_FIXTURES_OK\n";return 0;
    }catch(const std::exception& e){std::cerr<<"zenpkg-fixture-export: "<<e.what()<<"\n";return 1;}
}
