#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
#pragma pack(push, 1)
struct Header {
    char magic[4]; std::uint16_t schema, header_size;
    std::uint32_t version, engine, mode, count, payload_size, window_ticks, max_writes, max_renames, max_removes, max_bytes;
    std::uint8_t payload_sha[32], key_id[8], reserved[8];
};
struct Record { std::uint8_t type, operations; std::uint16_t path_length; char path[48]; std::uint8_t digest[32]; std::uint8_t reserved[12]; };
#pragma pack(pop)
static_assert(sizeof(Header) == 96U && sizeof(Record) == 96U);
constexpr std::uint8_t kProtected = 1U, kWriter = 2U, kWrite = 1U, kRename = 2U, kRemove = 4U;
struct Policy { Header header{}; std::vector<Record> records; };
struct Window { bool valid=false, locked=false; std::string actor; std::array<std::uint8_t,32> digest{}; std::uint32_t started=0,writes=0,renames=0,removes=0,bytes=0; };
enum class Decision { allow, audit, block };

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary); if (!input) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
Policy load(const std::string& path) {
    const auto bytes=read_all(path); if(bytes.size()<sizeof(Header)+256U) throw std::runtime_error("truncated policy");
    Policy p{}; std::memcpy(&p.header,bytes.data(),sizeof(Header));
    if(std::memcmp(p.header.magic,"ZRWP",4U)||p.header.count>16U||p.header.payload_size!=p.header.count*sizeof(Record)) throw std::runtime_error("bad policy");
    p.records.resize(p.header.count); std::memcpy(p.records.data(),bytes.data()+sizeof(Header),p.header.payload_size); return p;
}
bool path_match(const std::string& path,const char* prefix) { const std::string p(prefix); return path==p||(path.size()>p.size()&&path.compare(0,p.size(),p)==0&&path[p.size()]=='/'); }
bool protected_path(const Policy& p,const std::string& path,std::uint8_t op) { for(const auto&r:p.records) if(r.type==kProtected&&(r.operations&op)&&path_match(path,r.path)) return true; return false; }
bool writer(const Policy&p,const std::string& actor,const std::array<std::uint8_t,32>&digest,std::uint8_t op) { for(const auto&r:p.records) if(r.type==kWriter&&(r.operations&op)&&actor==r.path&&!std::memcmp(digest.data(),r.digest,32U)) return true; return false; }
Decision authorize(const Policy&p,Window&w,const std::string&actor,const std::array<std::uint8_t,32>&digest,const std::string&target,std::uint8_t op,std::uint32_t bytes,std::uint32_t tick) {
    if(!protected_path(p,target,op)||actor=="<kernel>") return Decision::allow;
    if(!w.valid||w.actor!=actor||w.digest!=digest||tick-w.started>=p.header.window_ticks) { w={};w.valid=true;w.actor=actor;w.digest=digest;w.started=tick; }
    const bool writeOverflow=op==kWrite&&w.writes==0xFFFFFFFFU,renameOverflow=op==kRename&&w.renames==0xFFFFFFFFU;
    const bool removeOverflow=op==kRemove&&w.removes==0xFFFFFFFFU,byteOverflow=bytes>0xFFFFFFFFU-w.bytes;
    const auto writes=writeOverflow?0xFFFFFFFFU:w.writes+(op==kWrite),renames=renameOverflow?0xFFFFFFFFU:w.renames+(op==kRename);
    const auto removes=removeOverflow?0xFFFFFFFFU:w.removes+(op==kRemove),total=byteOverflow?0xFFFFFFFFU:w.bytes+bytes;
    const bool violation=!writer(p,actor,digest,op)||w.locked||writeOverflow||renameOverflow||removeOverflow||byteOverflow||writes>p.header.max_writes||renames>p.header.max_renames||removes>p.header.max_removes||total>p.header.max_bytes;
    if(violation&&p.header.mode==1U){w.locked=true;return Decision::block;}
    w.writes=writes;w.renames=renames;w.removes=removes;w.bytes=total; return violation?Decision::audit:Decision::allow;
}
void require(bool value,const char*message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char**argv){try{
    if(argc!=3){std::cerr<<"usage: ransomware-policy-test <v1> <v2>\n";return 2;}
    const auto v1=load(argv[1]),v2=load(argv[2]);
    require(v1.header.version==1U&&v1.header.mode==0U,"v1 mode"); require(v2.header.version==2U&&v2.header.mode==1U,"v2 mode");
    std::array<std::uint8_t,32> fileio={0x5a,0xcc,0x70,0xa7,0x8b,0xd8,0x30,0xcd,0x7b,0x04,0x79,0x9b,0xf3,0xc2,0xbc,0x22,0x90,0x5a,0xc5,0x30,0x70,0xdb,0x00,0xb5,0x35,0x68,0x7c,0x8b,0x70,0x3a,0x93,0x4e};
    std::array<std::uint8_t,32> other{}; other[0]=1U; Window w{};
    require(authorize(v1,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,24U,1U)==Decision::allow,"audit first allow");
    require(authorize(v1,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,24U,2U)==Decision::audit,"audit budget");
    w={}; require(authorize(v2,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,24U,1U)==Decision::allow,"block first allow");
    require(authorize(v2,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,24U,2U)==Decision::block,"block budget");
    require(authorize(v2,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,1U,3U)==Decision::block,"locked actor");
    w={}; require(authorize(v2,w,"/apps/other.elf",other,"/docs/readme.txt",kRemove,0U,1U)==Decision::block,"untrusted writer");
    w={}; require(authorize(v2,w,"<kernel>",other,"/docs/readme.txt",kRemove,0U,1U)==Decision::allow,"kernel bypass");
    w={}; require(authorize(v2,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,24U,1U)==Decision::allow,"window initial");
    require(authorize(v2,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,24U,1U+v2.header.window_ticks)==Decision::allow,"window reset");
    require(protected_path(v2,"/docs/sub/file.txt",kRename),"prefix protection"); require(!protected_path(v2,"/doc/file.txt",kRename),"prefix boundary");
    w={}; w.valid=true; w.actor="/apps/fileio.elf"; w.digest=fileio; w.started=1U; w.writes=0xFFFFFFFFU; w.bytes=0xFFFFFFFFU;
    require(authorize(v2,w,"/apps/fileio.elf",fileio,"/apps/userio.txt",kWrite,1U,2U)==Decision::block,"counter overflow");
    std::cout<<"ZRWP_HOST_MODEL_OK audit_to_block=yes exact_writer=yes budgets=yes locked_actor=yes window_reset=yes prefix_boundary=yes overflow=blocked\n";return 0;
}catch(const std::exception&e){std::cerr<<"ransomware-policy-test: "<<e.what()<<"\n";return 1;}}
