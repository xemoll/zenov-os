#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint16_t kAuditCapacity = 64;
constexpr std::size_t kAuditBytes = 8288;

#pragma pack(push, 1)
struct Superblock {
    char magic[8];
    std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation;
    char label[16];
    std::uint8_t reserved[460];
};
struct Entry {
    std::uint8_t used, type;
    std::uint16_t flags;
    char path[48];
    std::uint32_t size, checksum, reserved;
};
struct AuditHeader {
    char magic[4];
    std::uint16_t schema, header_size, record_size, capacity;
    std::uint32_t count, next_index, next_sequence;
    std::uint8_t anchor_hash[32], head_hash[32], reserved[8];
};
struct AuditRecord {
    std::uint32_t sequence, tick;
    std::uint8_t action, verdict;
    std::uint16_t path_length;
    char path[48];
    std::uint8_t digest[32], record_hash[32], reserved[4];
};
struct AuditJournal { AuditHeader header; AuditRecord records[kAuditCapacity]; };
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize && sizeof(Entry) == 64);
static_assert(sizeof(AuditHeader) == 96 && sizeof(AuditRecord) == 128 && sizeof(AuditJournal) == kAuditBytes);

struct Sha256 {
    std::array<std::uint32_t, 8> state{};
    std::uint64_t total = 0;
    std::array<std::uint8_t, 64> block{};
    std::size_t used = 0;
};

constexpr std::array<std::uint32_t, 64> kSha256 = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U,
};

std::uint32_t rotate_right(std::uint32_t value, unsigned count) { return (value >> count) | (value << (32U - count)); }
std::uint32_t load_be32(const std::uint8_t* input) {
    return (static_cast<std::uint32_t>(input[0]) << 24U) | (static_cast<std::uint32_t>(input[1]) << 16U) |
        (static_cast<std::uint32_t>(input[2]) << 8U) | input[3];
}
void store_be32(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 24U); output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U); output[3] = static_cast<std::uint8_t>(value);
}
void sha_transform(Sha256& context, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) words[i] = load_be32(block + i * 4U);
    for (std::size_t i = 16; i < words.size(); ++i) {
        const std::uint32_t s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3U);
        const std::uint32_t s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10U);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    std::uint32_t a=context.state[0],b=context.state[1],c=context.state[2],d=context.state[3];
    std::uint32_t e=context.state[4],f=context.state[5],g=context.state[6],h=context.state[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
        const std::uint32_t ch=(e&f)^(~e&g);
        const std::uint32_t t1=h+s1+ch+kSha256[i]+words[i];
        const std::uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
        const std::uint32_t maj=(a&b)^(a&c)^(b&c);
        const std::uint32_t t2=s0+maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    context.state[0]+=a; context.state[1]+=b; context.state[2]+=c; context.state[3]+=d;
    context.state[4]+=e; context.state[5]+=f; context.state[6]+=g; context.state[7]+=h;
}
void sha_init(Sha256& context) {
    context.state={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    context.total=0; context.used=0;
}
void sha_update(Sha256& context, const std::uint8_t* data, std::size_t size) {
    context.total += size;
    for (std::size_t i=0;i<size;++i) {
        context.block[context.used++]=data[i];
        if (context.used==context.block.size()) { sha_transform(context, context.block.data()); context.used=0; }
    }
}
std::array<std::uint8_t,32> sha_final(Sha256& context) {
    const std::uint64_t bits=context.total*8U;
    context.block[context.used++]=0x80U;
    if (context.used>56U) { while(context.used<64U) context.block[context.used++]=0; sha_transform(context,context.block.data()); context.used=0; }
    while(context.used<56U) context.block[context.used++]=0;
    for(std::size_t i=0;i<8;++i) context.block[63U-i]=static_cast<std::uint8_t>(bits>>(i*8U));
    sha_transform(context,context.block.data());
    std::array<std::uint8_t,32> output{};
    for(std::size_t i=0;i<8;++i) store_be32(output.data()+i*4U,context.state[i]);
    return output;
}

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash=2166136261U;
    for(std::size_t i=0;i<size;++i){hash^=data[i];hash*=16777619U;}
    return hash;
}
bool all_zero(const std::uint8_t* data, std::size_t size) {
    std::uint8_t value=0; for(std::size_t i=0;i<size;++i)value=static_cast<std::uint8_t>(value|data[i]); return value==0;
}
bool same_hash(const std::uint8_t* left,const std::uint8_t* right){std::uint8_t d=0;for(std::size_t i=0;i<32;++i)d=static_cast<std::uint8_t>(d|(left[i]^right[i]));return d==0;}
void store_le16(std::uint8_t* output,std::uint16_t value){output[0]=static_cast<std::uint8_t>(value);output[1]=static_cast<std::uint8_t>(value>>8U);}
void store_le32(std::uint8_t* output,std::uint32_t value){output[0]=static_cast<std::uint8_t>(value);output[1]=static_cast<std::uint8_t>(value>>8U);output[2]=static_cast<std::uint8_t>(value>>16U);output[3]=static_cast<std::uint8_t>(value>>24U);}
std::array<std::uint8_t,32> record_hash(const AuditRecord& record,const std::uint8_t previous[32]){
    constexpr std::array<std::uint8_t,16> domain={'Z','E','N','O','V','-','A','U','D','I','T','-','V','1',0,0};
    std::array<std::uint8_t,12> metadata{};
    store_le32(metadata.data(),record.sequence);store_le32(metadata.data()+4,record.tick);metadata[8]=record.action;metadata[9]=record.verdict;store_le16(metadata.data()+10,record.path_length);
    Sha256 context{};sha_init(context);sha_update(context,domain.data(),domain.size());sha_update(context,previous,32);sha_update(context,metadata.data(),metadata.size());
    sha_update(context,reinterpret_cast<const std::uint8_t*>(record.path),sizeof(record.path));sha_update(context,record.digest,sizeof(record.digest));return sha_final(context);
}

struct LocatedJournal { Superblock* super; Entry* entries; Entry* entry; std::size_t entry_index; std::size_t data_offset; AuditJournal* journal; };
std::vector<std::uint8_t> read_image(const std::string& path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot open image");input.seekg(0,std::ios::end);const auto length=input.tellg();if(length<static_cast<std::streamoff>(kSectorSize))throw std::runtime_error("image too small");input.seekg(0);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));input.read(reinterpret_cast<char*>(bytes.data()),length);if(!input)throw std::runtime_error("cannot read image");return bytes;}
void write_image(const std::string& path,const std::vector<std::uint8_t>& image){std::ofstream output(path,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("cannot open output image");output.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));if(!output)throw std::runtime_error("cannot write output image");}
LocatedJournal locate(std::vector<std::uint8_t>& image){
    auto* super=reinterpret_cast<Superblock*>(image.data());
    if(std::memcmp(super->magic,"ZENOVFS1",8)!=0||super->version!=1||!super->entry_count||super->entry_count>128)throw std::runtime_error("invalid ZenovFS superblock");
    if(image.size()<static_cast<std::size_t>(super->total_sectors)*kSectorSize)throw std::runtime_error("truncated ZenovFS image");
    auto* entries=reinterpret_cast<Entry*>(image.data()+kSectorSize);
    for(std::size_t i=0;i<super->entry_count;++i){
        if(!entries[i].used||entries[i].type!=1||std::strncmp(entries[i].path,"/security/zenovguard.audit",sizeof(entries[i].path))!=0)continue;
        if(entries[i].size!=sizeof(AuditJournal))throw std::runtime_error("audit file has wrong size");
        const std::size_t offset=(static_cast<std::size_t>(super->data_start)+i*super->slot_sectors)*kSectorSize;
        if(offset+entries[i].size>image.size())throw std::runtime_error("audit data outside image");
        if(fnv1a(image.data()+offset,entries[i].size)!=entries[i].checksum)throw std::runtime_error("audit ZenovFS checksum mismatch");
        return {super,entries,&entries[i],i,offset,reinterpret_cast<AuditJournal*>(image.data()+offset)};
    }
    throw std::runtime_error("audit journal not found");
}
void verify_journal(const AuditJournal& journal,bool require_nonempty){
    const auto& h=journal.header;
    if(std::memcmp(h.magic,"ZGAL",4)!=0||h.schema!=1||h.header_size!=sizeof(AuditHeader)||h.record_size!=sizeof(AuditRecord)||h.capacity!=kAuditCapacity||h.count>kAuditCapacity||h.next_index>=kAuditCapacity||!h.next_sequence||!all_zero(h.reserved,sizeof(h.reserved)))throw std::runtime_error("invalid audit header");
    if(require_nonempty&&!h.count)throw std::runtime_error("audit journal unexpectedly empty");
    if(!h.count){if(h.next_index||h.next_sequence!=1||!all_zero(h.anchor_hash,32)||!all_zero(h.head_hash,32))throw std::runtime_error("non-canonical empty journal");return;}
    if(h.next_sequence<=h.count)throw std::runtime_error("invalid audit sequence range");
    if(h.count<kAuditCapacity&&(h.next_index!=h.count||!all_zero(h.anchor_hash,32)))throw std::runtime_error("invalid partial ring state");
    const std::uint32_t first=h.count==kAuditCapacity?h.next_index:0;
    const std::uint32_t first_sequence=h.next_sequence-h.count;
    std::array<std::uint8_t,32> previous{};std::memcpy(previous.data(),h.anchor_hash,32);
    for(std::uint32_t i=0;i<h.count;++i){
        const auto& record=journal.records[(first+i)%kAuditCapacity];
        if(record.sequence!=first_sequence+i||record.action>3||record.verdict>5||!record.path_length||record.path_length>=sizeof(record.path)||record.path[record.path_length]!=0||!all_zero(record.reserved,sizeof(record.reserved)))throw std::runtime_error("invalid audit record");
        for(std::size_t p=record.path_length+1;p<sizeof(record.path);++p)if(record.path[p]!=0)throw std::runtime_error("non-canonical audit path padding");
        const auto computed=record_hash(record,previous.data());if(!same_hash(computed.data(),record.record_hash))throw std::runtime_error("audit hash-chain mismatch");previous=computed;
    }
    if(!same_hash(previous.data(),h.head_hash))throw std::runtime_error("audit head hash mismatch");
}
std::string prefix(const std::uint8_t hash[32]){std::ostringstream out;out<<std::hex<<std::setfill('0');for(std::size_t i=0;i<8;++i)out<<std::setw(2)<<static_cast<unsigned>(hash[i]);return out.str();}
}

int main(int argc,char** argv){
    try{
        if(argc<2||argc>5){std::cerr<<"usage: zenovfs-audit-verify <image> [--require-nonempty] [--emit-tampered <output>]\n";return 2;}
        bool require_nonempty=false;std::string tampered_path;
        for(int i=2;i<argc;++i){const std::string argument=argv[i];if(argument=="--require-nonempty")require_nonempty=true;else if(argument=="--emit-tampered"&&i+1<argc)tampered_path=argv[++i];else throw std::runtime_error("unknown argument: "+argument);}
        auto image=read_image(argv[1]);auto located=locate(image);verify_journal(*located.journal,require_nonempty);
        std::cout<<"zenovfs-audit-verify: OK count="<<located.journal->header.count<<" next="<<located.journal->header.next_sequence<<" head="<<prefix(located.journal->header.head_hash)<<"\n";
        if(!tampered_path.empty()){
            if(located.journal->header.count){const std::uint32_t first=located.journal->header.count==kAuditCapacity?located.journal->header.next_index:0;located.journal->records[first].path[0]^=1;}
            else located.journal->header.reserved[0]=1;
            located.entry->checksum=fnv1a(image.data()+located.data_offset,located.entry->size);
            write_image(tampered_path,image);
            std::cout<<"zenovfs-audit-verify: tampered fixture written with valid ZenovFS checksum\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"zenovfs-audit-verify: "<<error.what()<<"\n";return 1;}
}
