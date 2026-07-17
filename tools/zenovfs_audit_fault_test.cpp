#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kEntryLimit = 128U;
constexpr std::uint16_t kAuditCapacity = 64U;
constexpr std::size_t kAuditBytes = 8288U;
constexpr std::uint8_t kFile = 1U;
constexpr std::uint8_t kTransaction = 3U;
constexpr std::uint16_t kCommitted = 1U;
constexpr char kAuditPath[] = "/security/zenovguard.audit";

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

static_assert(sizeof(Superblock) == kSectorSize);
static_assert(sizeof(Entry) == 64U);
static_assert(sizeof(AuditHeader) == 96U);
static_assert(sizeof(AuditRecord) == 128U);
static_assert(sizeof(AuditJournal) == kAuditBytes);

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
    for (std::size_t i = 0; i < 16U; ++i) words[i] = load_be32(block + i * 4U);
    for (std::size_t i = 16U; i < words.size(); ++i) {
        const std::uint32_t s0 = rotate_right(words[i - 15U], 7U) ^ rotate_right(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
        const std::uint32_t s1 = rotate_right(words[i - 2U], 17U) ^ rotate_right(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }
    std::uint32_t a=context.state[0],b=context.state[1],c=context.state[2],d=context.state[3];
    std::uint32_t e=context.state[4],f=context.state[5],g=context.state[6],h=context.state[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t s1=rotate_right(e,6U)^rotate_right(e,11U)^rotate_right(e,25U);
        const std::uint32_t choice=(e&f)^(~e&g);
        const std::uint32_t temp1=h+s1+choice+kSha256[i]+words[i];
        const std::uint32_t s0=rotate_right(a,2U)^rotate_right(a,13U)^rotate_right(a,22U);
        const std::uint32_t majority=(a&b)^(a&c)^(b&c);
        const std::uint32_t temp2=s0+majority;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
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
    for (std::size_t i = 0; i < size; ++i) {
        context.block[context.used++] = data[i];
        if (context.used == context.block.size()) { sha_transform(context, context.block.data()); context.used = 0; }
    }
}
std::array<std::uint8_t, 32> sha_final(Sha256& context) {
    const std::uint64_t bits = context.total * 8U;
    context.block[context.used++] = 0x80U;
    if (context.used > 56U) { while (context.used < 64U) context.block[context.used++] = 0; sha_transform(context, context.block.data()); context.used = 0; }
    while (context.used < 56U) context.block[context.used++] = 0;
    for (std::size_t i = 0; i < 8U; ++i) context.block[63U - i] = static_cast<std::uint8_t>(bits >> (i * 8U));
    sha_transform(context, context.block.data());
    std::array<std::uint8_t, 32> output{};
    for (std::size_t i = 0; i < 8U; ++i) store_be32(output.data() + i * 4U, context.state[i]);
    return output;
}

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
}
bool all_zero(const std::uint8_t* data, std::size_t size) {
    std::uint8_t combined = 0;
    for (std::size_t i = 0; i < size; ++i) combined = static_cast<std::uint8_t>(combined | data[i]);
    return combined == 0;
}
bool same_hash(const std::uint8_t* left, const std::uint8_t* right) {
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < 32U; ++i) difference = static_cast<std::uint8_t>(difference | (left[i] ^ right[i]));
    return difference == 0;
}
void store_le16(std::uint8_t* output, std::uint16_t value) { output[0]=static_cast<std::uint8_t>(value); output[1]=static_cast<std::uint8_t>(value>>8U); }
void store_le32(std::uint8_t* output, std::uint32_t value) {
    output[0]=static_cast<std::uint8_t>(value); output[1]=static_cast<std::uint8_t>(value>>8U);
    output[2]=static_cast<std::uint8_t>(value>>16U); output[3]=static_cast<std::uint8_t>(value>>24U);
}
std::array<std::uint8_t, 32> record_hash(const AuditRecord& record, const std::uint8_t previous[32]) {
    constexpr std::array<std::uint8_t, 16> domain={'Z','E','N','O','V','-','A','U','D','I','T','-','V','1',0,0};
    std::array<std::uint8_t, 12> metadata{};
    store_le32(metadata.data(),record.sequence); store_le32(metadata.data()+4U,record.tick);
    metadata[8]=record.action; metadata[9]=record.verdict; store_le16(metadata.data()+10U,record.path_length);
    Sha256 context{}; sha_init(context); sha_update(context,domain.data(),domain.size()); sha_update(context,previous,32U);
    sha_update(context,metadata.data(),metadata.size()); sha_update(context,reinterpret_cast<const std::uint8_t*>(record.path),sizeof(record.path));
    sha_update(context,record.digest,sizeof(record.digest)); return sha_final(context);
}

void initialize_empty(AuditJournal& journal) {
    std::memset(&journal, 0, sizeof(journal));
    std::memcpy(journal.header.magic, "ZGAL", 4U);
    journal.header.schema = 1U; journal.header.header_size = sizeof(AuditHeader); journal.header.record_size = sizeof(AuditRecord);
    journal.header.capacity = kAuditCapacity; journal.header.next_sequence = 1U;
}
bool verify_journal(const AuditJournal& journal) {
    const AuditHeader& header = journal.header;
    if (std::memcmp(header.magic,"ZGAL",4U)!=0 || header.schema!=1U || header.header_size!=sizeof(AuditHeader) ||
        header.record_size!=sizeof(AuditRecord) || header.capacity!=kAuditCapacity || header.count>kAuditCapacity ||
        header.next_index>=kAuditCapacity || !header.next_sequence || !all_zero(header.reserved,sizeof(header.reserved))) return false;
    if (!header.count) return header.next_index==0U && header.next_sequence==1U && all_zero(header.anchor_hash,32U) && all_zero(header.head_hash,32U);
    if (header.next_sequence<=header.count) return false;
    if (header.count<kAuditCapacity && (header.next_index!=header.count || !all_zero(header.anchor_hash,32U))) return false;
    const std::uint32_t first=header.count==kAuditCapacity?header.next_index:0U;
    const std::uint32_t first_sequence=header.next_sequence-header.count;
    std::array<std::uint8_t,32> previous{}; std::memcpy(previous.data(),header.anchor_hash,32U);
    for (std::uint32_t i=0;i<header.count;++i) {
        const AuditRecord& record=journal.records[(first+i)%kAuditCapacity];
        if (record.sequence!=first_sequence+i || record.action>3U || record.verdict>5U || !record.path_length ||
            record.path_length>=sizeof(record.path) || record.path[record.path_length]!=0 || !all_zero(record.reserved,sizeof(record.reserved))) return false;
        for (std::size_t p=record.path_length+1U;p<sizeof(record.path);++p) if (record.path[p]!=0) return false;
        const auto computed=record_hash(record,previous.data()); if (!same_hash(computed.data(),record.record_hash)) return false; previous=computed;
    }
    return same_hash(previous.data(),header.head_hash);
}
std::array<std::uint8_t, 32> event_digest(std::uint32_t sequence) {
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) digest[i] = static_cast<std::uint8_t>((sequence * 37U + i * 13U) & 0xFFU);
    return digest;
}
std::string event_path(std::uint32_t sequence) { return "/fault/audit-" + std::to_string(sequence); }
void append_event(AuditJournal& journal, std::uint32_t tick) {
    AuditHeader& header=journal.header;
    if (header.next_sequence==0xFFFFFFFFU) throw std::runtime_error("audit sequence exhausted");
    const std::uint32_t index=header.next_index;
    if (header.count==kAuditCapacity) std::memcpy(header.anchor_hash,journal.records[index].record_hash,32U);
    AuditRecord& record=journal.records[index]; std::memset(&record,0,sizeof(record));
    record.sequence=header.next_sequence; record.tick=tick; record.action=2U; record.verdict=1U;
    const std::string path=event_path(record.sequence);
    if (path.size()+1U>sizeof(record.path)) throw std::runtime_error("fault path too long");
    std::memcpy(record.path,path.c_str(),path.size()+1U); record.path_length=static_cast<std::uint16_t>(path.size());
    const auto digest=event_digest(record.sequence); std::memcpy(record.digest,digest.data(),digest.size());
    const auto hash=record_hash(record,header.head_hash); std::memcpy(record.record_hash,hash.data(),hash.size());
    if (header.count<kAuditCapacity) ++header.count;
    header.next_index=(index+1U)%kAuditCapacity; ++header.next_sequence; std::memcpy(header.head_hash,record.record_hash,32U);
    if (!verify_journal(journal)) throw std::runtime_error("generated audit journal is invalid");
}

std::vector<std::uint8_t> read_image(const std::string& path) {
    std::ifstream input(path,std::ios::binary); if(!input) throw std::runtime_error("cannot open image");
    input.seekg(0,std::ios::end); const auto length=input.tellg(); if(length<static_cast<std::streamoff>(kSectorSize)) throw std::runtime_error("image too small");
    input.seekg(0); std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())); if(!input) throw std::runtime_error("cannot read image"); return bytes;
}
void write_image(const std::string& path,const std::vector<std::uint8_t>& image) {
    std::ofstream output(path,std::ios::binary|std::ios::trunc); if(!output) throw std::runtime_error("cannot create output image");
    output.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size())); if(!output) throw std::runtime_error("cannot write output image");
}
Superblock& super(std::vector<std::uint8_t>& disk) { return *reinterpret_cast<Superblock*>(disk.data()); }
const Superblock& super(const std::vector<std::uint8_t>& disk) { return *reinterpret_cast<const Superblock*>(disk.data()); }
Entry* entries(std::vector<std::uint8_t>& disk) { return reinterpret_cast<Entry*>(disk.data()+kSectorSize); }
const Entry* entries(const std::vector<std::uint8_t>& disk) { return reinterpret_cast<const Entry*>(disk.data()+kSectorSize); }
std::uint32_t metadata_sector(std::uint32_t index) { return 1U+index/8U; }
std::uint32_t slot_offset(std::uint32_t index) { return (index%8U)*sizeof(Entry); }
std::uint32_t data_sector(const Superblock& block,std::uint32_t index) { return block.data_start+index*block.slot_sectors; }
bool path_equal(const Entry& entry,const char* path) {
    const std::size_t length=std::strlen(path);
    return length<sizeof(entry.path) && std::memcmp(entry.path,path,length)==0 && entry.path[length]==0;
}
void validate_image(const std::vector<std::uint8_t>& disk) {
    if (disk.size()<kSectorSize || std::memcmp(disk.data(),"ZENOVFS1",8U)!=0) throw std::runtime_error("not a ZenovFS1 image");
    const Superblock& block=super(disk);
    if (!block.entry_count || block.entry_count>kEntryLimit || !block.slot_sectors ||
        disk.size()<static_cast<std::size_t>(block.total_sectors)*kSectorSize) throw std::runtime_error("invalid ZenovFS geometry");
}
int find_audit(const std::vector<std::uint8_t>& disk) {
    const Superblock& block=super(disk); const Entry* table=entries(disk); int found=-1;
    for (std::uint32_t i=0;i<block.entry_count;++i) if(table[i].used && table[i].type==kFile && path_equal(table[i],kAuditPath)) {
        if(found>=0) throw std::runtime_error("duplicate audit path in source image"); found=static_cast<int>(i);
    }
    if(found<0) throw std::runtime_error("audit path missing from source image"); return found;
}
int find_free(const std::vector<std::uint8_t>& disk,std::uint32_t excluded) {
    const Superblock& block=super(disk); const Entry* table=entries(disk);
    for(std::uint32_t i=0;i<block.entry_count;++i) if(i!=excluded && !table[i].used) return static_cast<int>(i);
    return -1;
}
AuditJournal read_audit(const std::vector<std::uint8_t>& disk,std::uint32_t index) {
    const Superblock& block=super(disk); const Entry& entry=entries(disk)[index];
    if(!entry.used||entry.type!=kFile||entry.flags||entry.size!=sizeof(AuditJournal)||!path_equal(entry,kAuditPath)) throw std::runtime_error("invalid audit metadata");
    const std::size_t offset=static_cast<std::size_t>(data_sector(block,index))*kSectorSize;
    if(offset+sizeof(AuditJournal)>disk.size()) throw std::runtime_error("audit payload outside image");
    AuditJournal journal{}; std::memcpy(&journal,disk.data()+offset,sizeof(journal));
    if(fnv1a(reinterpret_cast<const std::uint8_t*>(&journal),sizeof(journal))!=entry.checksum||!verify_journal(journal)) throw std::runtime_error("invalid source audit journal");
    return journal;
}
void patch_audit(std::vector<std::uint8_t>& disk,std::uint32_t index,const AuditJournal& journal) {
    Entry& entry=entries(disk)[index]; const std::size_t offset=static_cast<std::size_t>(data_sector(super(disk),index))*kSectorSize;
    std::memcpy(disk.data()+offset,&journal,sizeof(journal));
    entry.size=sizeof(journal); entry.checksum=fnv1a(reinterpret_cast<const std::uint8_t*>(&journal),sizeof(journal)); entry.flags=0; entry.reserved=0;
}

struct Write { std::uint32_t sector=0; std::array<std::uint8_t,kSectorSize> bytes{}; };
void apply_full(std::vector<std::uint8_t>& disk,const Write& write) {
    const std::size_t offset=static_cast<std::size_t>(write.sector)*kSectorSize;
    if(offset+kSectorSize>disk.size()) throw std::runtime_error("write outside image");
    std::copy(write.bytes.begin(),write.bytes.end(),disk.begin()+static_cast<std::ptrdiff_t>(offset));
}
void apply_torn(std::vector<std::uint8_t>& disk,const Write& write,std::size_t count,bool suffix) {
    const std::size_t offset=static_cast<std::size_t>(write.sector)*kSectorSize;
    if(!count||count>=kSectorSize||offset+kSectorSize>disk.size()) throw std::runtime_error("invalid torn write");
    const std::size_t start=suffix?kSectorSize-count:0U;
    std::copy_n(write.bytes.begin()+static_cast<std::ptrdiff_t>(start),count,disk.begin()+static_cast<std::ptrdiff_t>(offset+start));
}
void apply_garbage(std::vector<std::uint8_t>& disk,const Write& write,std::uint32_t seed) {
    const std::size_t offset=static_cast<std::size_t>(write.sector)*kSectorSize;
    if(offset+kSectorSize>disk.size()) throw std::runtime_error("garbage write outside image");
    for(std::size_t i=0;i<kSectorSize;++i) disk[offset+i]=static_cast<std::uint8_t>((seed*29U+i*131U+0x5AU)&0xFFU);
}
Write sector_snapshot(const std::vector<std::uint8_t>& disk,std::uint32_t sector) {
    Write write{}; write.sector=sector; const std::size_t offset=static_cast<std::size_t>(sector)*kSectorSize;
    std::copy_n(disk.begin()+static_cast<std::ptrdiff_t>(offset),kSectorSize,write.bytes.begin()); return write;
}
void set_entry_write(std::vector<std::uint8_t>& planning,std::vector<Write>& writes,std::uint32_t index,const Entry& entry) {
    const std::uint32_t sector=metadata_sector(index); const std::size_t offset=static_cast<std::size_t>(sector)*kSectorSize+slot_offset(index);
    std::memcpy(planning.data()+offset,&entry,sizeof(entry)); writes.push_back(sector_snapshot(planning,sector));
}
void clear_entry_write(std::vector<std::uint8_t>& planning,std::vector<Write>& writes,std::uint32_t index) { set_entry_write(planning,writes,index,Entry{}); }

struct Plan { std::vector<Write> writes; std::size_t payload_writes=0; std::uint32_t old_index=0, staging_index=0; };
Plan plan_replacement(const std::vector<std::uint8_t>& base,const AuditJournal& next) {
    Plan plan{}; plan.old_index=static_cast<std::uint32_t>(find_audit(base));
    const int free=find_free(base,plan.old_index); if(free<0) throw std::runtime_error("no free staging slot"); plan.staging_index=static_cast<std::uint32_t>(free);
    std::vector<std::uint8_t> planning=base; const auto* data=reinterpret_cast<const std::uint8_t*>(&next); std::size_t copied=0;
    for(std::uint32_t sector=0;copied<sizeof(next);++sector) {
        Write write{}; write.sector=data_sector(super(base),plan.staging_index)+sector;
        const std::size_t chunk=std::min<std::size_t>(kSectorSize,sizeof(next)-copied);
        std::copy_n(data+copied,chunk,write.bytes.begin()); apply_full(planning,write); plan.writes.push_back(write); copied+=chunk;
    }
    plan.payload_writes=plan.writes.size();
    Entry stage{}; stage.used=1U; stage.type=kTransaction; stage.size=sizeof(next);
    stage.checksum=fnv1a(reinterpret_cast<const std::uint8_t*>(&next),sizeof(next)); stage.reserved=plan.old_index;
    std::memcpy(stage.path,kAuditPath,sizeof(kAuditPath));
    set_entry_write(planning,plan.writes,plan.staging_index,stage);
    stage.type=kFile; stage.flags=kCommitted; set_entry_write(planning,plan.writes,plan.staging_index,stage);
    clear_entry_write(planning,plan.writes,plan.old_index);
    stage.flags=0; stage.reserved=0; set_entry_write(planning,plan.writes,plan.staging_index,stage);
    return plan;
}
void recover(std::vector<std::uint8_t>& disk) {
    Entry* table=entries(disk); const Superblock& block=super(disk);
    for(std::uint32_t i=0;i<block.entry_count;++i) if(table[i].used&&table[i].type==kTransaction) table[i]=Entry{};
    for(std::uint32_t i=0;i<block.entry_count;++i) {
        Entry& item=table[i]; if(!item.used||item.type!=kFile||!(item.flags&kCommitted)) continue;
        if(item.reserved<block.entry_count&&item.reserved!=i) table[item.reserved]=Entry{};
        item.flags=0; item.reserved=0;
    }
}

enum class Outcome { old_state,new_state,fail_closed };
Outcome classify(std::vector<std::uint8_t> disk,const AuditJournal& old_journal,const AuditJournal& new_journal) {
    recover(disk); const Superblock& block=super(disk); const Entry* table=entries(disk); int found=-1;
    for(std::uint32_t i=0;i<block.entry_count;++i) if(table[i].used&&table[i].type==kFile&&path_equal(table[i],kAuditPath)) {
        if(found>=0) return Outcome::fail_closed; found=static_cast<int>(i);
    }
    if(found<0) return Outcome::fail_closed;
    const Entry& entry=table[static_cast<std::uint32_t>(found)];
    if(entry.flags||entry.size!=sizeof(AuditJournal)) return Outcome::fail_closed;
    const std::size_t offset=static_cast<std::size_t>(data_sector(block,static_cast<std::uint32_t>(found)))*kSectorSize;
    if(offset+sizeof(AuditJournal)>disk.size()) return Outcome::fail_closed;
    AuditJournal candidate{}; std::memcpy(&candidate,disk.data()+offset,sizeof(candidate));
    if(fnv1a(reinterpret_cast<const std::uint8_t*>(&candidate),sizeof(candidate))!=entry.checksum||!verify_journal(candidate)) return Outcome::fail_closed;
    if(std::memcmp(&candidate,&old_journal,sizeof(candidate))==0) return Outcome::old_state;
    if(std::memcmp(&candidate,&new_journal,sizeof(candidate))==0) return Outcome::new_state;
    throw std::runtime_error("fault produced an unexpected but valid audit journal");
}

struct Counts { std::uint64_t old_state=0,new_state=0,fail_closed=0,cases=0; };
void observe(Counts& counts,Outcome outcome) {
    ++counts.cases;
    if(outcome==Outcome::old_state) ++counts.old_state;
    else if(outcome==Outcome::new_state) ++counts.new_state;
    else ++counts.fail_closed;
}
void require_recoverable(Outcome outcome,const std::string& label) {
    if(outcome==Outcome::fail_closed) throw std::runtime_error(label+" unexpectedly failed closed");
}

Counts run_fault_matrix(const std::string& name,const std::vector<std::uint8_t>& base,const AuditJournal& old_journal,const AuditJournal& new_journal,const Plan& plan) {
    Counts counts{}; bool saw_old=false,saw_new=false;
    for(std::size_t prefix=0;prefix<=plan.writes.size();++prefix) {
        auto crashed=base; for(std::size_t i=0;i<prefix;++i) apply_full(crashed,plan.writes[i]);
        const Outcome outcome=classify(crashed,old_journal,new_journal); require_recoverable(outcome,name+" ordered prefix "+std::to_string(prefix));
        saw_old=saw_old||outcome==Outcome::old_state; saw_new=saw_new||outcome==Outcome::new_state; observe(counts,outcome);
    }
    if(!saw_old||!saw_new) throw std::runtime_error(name+" ordered prefixes did not expose both atomic states");

    constexpr std::array<std::size_t,7> cuts={1U,64U,128U,255U,256U,384U,511U};
    for(std::size_t index=0;index<plan.writes.size();++index) for(std::size_t cut:cuts) for(bool suffix:{false,true}) {
        auto crashed=base; for(std::size_t i=0;i<index;++i) apply_full(crashed,plan.writes[i]);
        apply_torn(crashed,plan.writes[index],cut,suffix); observe(counts,classify(crashed,old_journal,new_journal));
    }
    for(std::size_t index=0;index<plan.writes.size();++index) {
        auto crashed=base; for(std::size_t i=0;i<index;++i) apply_full(crashed,plan.writes[i]);
        apply_garbage(crashed,plan.writes[index],static_cast<std::uint32_t>(index+1U)); observe(counts,classify(crashed,old_journal,new_journal));
    }
    for(std::size_t dropped=0;dropped<plan.writes.size();++dropped) {
        auto crashed=base; for(std::size_t i=0;i<plan.writes.size();++i) if(i!=dropped) apply_full(crashed,plan.writes[i]);
        observe(counts,classify(crashed,old_journal,new_journal));
    }
    for(std::size_t duplicate=0;duplicate<plan.writes.size();++duplicate) {
        auto crashed=base; for(std::size_t i=0;i<plan.writes.size();++i) { apply_full(crashed,plan.writes[i]); if(i==duplicate) apply_full(crashed,plan.writes[i]); }
        const Outcome outcome=classify(crashed,old_journal,new_journal); if(outcome!=Outcome::new_state) throw std::runtime_error(name+" duplicate write was not idempotent"); observe(counts,outcome);
    }
    for(std::size_t swapped=0;swapped+1U<plan.writes.size();++swapped) {
        auto crashed=base;
        for(std::size_t i=0;i<plan.writes.size();++i) {
            if(i==swapped) apply_full(crashed,plan.writes[i+1U]);
            else if(i==swapped+1U) apply_full(crashed,plan.writes[i-1U]);
            else apply_full(crashed,plan.writes[i]);
        }
        observe(counts,classify(crashed,old_journal,new_journal));
    }

    std::array<std::size_t,4> metadata={plan.payload_writes,plan.payload_writes+1U,plan.payload_writes+2U,plan.payload_writes+3U};
    do {
        for(std::size_t payload_prefix=0;payload_prefix<=plan.payload_writes;++payload_prefix) {
            auto crashed=base; for(std::size_t i=0;i<payload_prefix;++i) apply_full(crashed,plan.writes[i]);
            for(std::size_t index:metadata) apply_full(crashed,plan.writes[index]);
            observe(counts,classify(crashed,old_journal,new_journal));
        }
    } while(std::next_permutation(metadata.begin(),metadata.end()));

    if(!counts.fail_closed) throw std::runtime_error(name+" destructive matrix never exercised fail-closed behavior");
    std::cout<<"ZENOV_AUDIT_COW_FAULT_MATRIX_OK scenario="<<name<<" cases="<<counts.cases
             <<" old="<<counts.old_state<<" new="<<counts.new_state<<" fail_closed="<<counts.fail_closed<<"\n";
    return counts;
}

void apply_prefix(std::vector<std::uint8_t>& disk,const Plan& plan,std::size_t prefix) {
    if(prefix>plan.writes.size()) throw std::runtime_error("invalid recovery prefix");
    for(std::size_t i=0;i<prefix;++i) apply_full(disk,plan.writes[i]);
}
}

int main(int argc,char** argv) {
    try {
        if(argc<2) { std::cerr<<"usage: zenovfs-audit-fault-test <zenov-data.img> [--emit-old-recovery <img>] [--emit-new-recovery <img>] [--emit-corrupt <img>]\n"; return 2; }
        std::string old_recovery_path,new_recovery_path,corrupt_path;
        for(int i=2;i<argc;++i) {
            const std::string argument=argv[i];
            if(argument=="--emit-old-recovery"&&i+1<argc) old_recovery_path=argv[++i];
            else if(argument=="--emit-new-recovery"&&i+1<argc) new_recovery_path=argv[++i];
            else if(argument=="--emit-corrupt"&&i+1<argc) corrupt_path=argv[++i];
            else throw std::runtime_error("unknown or incomplete argument: "+argument);
        }
        const auto original=read_image(argv[1]); validate_image(original);
        const std::uint32_t original_index=static_cast<std::uint32_t>(find_audit(original));
        const AuditJournal empty=read_audit(original,original_index);
        if(empty.header.count!=0U) throw std::runtime_error("factory audit journal must be empty");

        AuditJournal one=empty; append_event(one,100U); const Plan empty_plan=plan_replacement(original,one);
        const Counts empty_counts=run_fault_matrix("empty-to-one",original,empty,one,empty_plan);

        auto full_base=original; AuditJournal full{}; initialize_empty(full);
        for(std::uint32_t i=0;i<kAuditCapacity;++i) append_event(full,1000U+i);
        patch_audit(full_base,original_index,full);
        AuditJournal rotated=full; append_event(rotated,2000U); const Plan rotation_plan=plan_replacement(full_base,rotated);
        const Counts rotation_counts=run_fault_matrix("full-ring-rotation",full_base,full,rotated,rotation_plan);

        if(!old_recovery_path.empty()) {
            auto image=original; apply_prefix(image,empty_plan,empty_plan.payload_writes+1U); write_image(old_recovery_path,image);
            std::cout<<"ZENOV_AUDIT_OLD_RECOVERY_IMAGE_OK output="<<old_recovery_path<<"\n";
        }
        if(!new_recovery_path.empty()) {
            auto image=original; apply_prefix(image,empty_plan,empty_plan.payload_writes+2U); write_image(new_recovery_path,image);
            std::cout<<"ZENOV_AUDIT_NEW_RECOVERY_IMAGE_OK output="<<new_recovery_path<<"\n";
        }
        if(!corrupt_path.empty()) {
            auto image=original;
            for(std::size_t i=0;i<empty_plan.writes.size();++i) if(i!=0U) apply_full(image,empty_plan.writes[i]);
            write_image(corrupt_path,image); std::cout<<"ZENOV_AUDIT_FAIL_CLOSED_IMAGE_OK output="<<corrupt_path<<"\n";
        }

        const std::uint64_t total=empty_counts.cases+rotation_counts.cases;
        std::cout<<"ZENOV_AUDIT_COW_FAULT_INJECTION_OK total_cases="<<total<<"\n";
        std::cout<<"ZENOV_AUDIT_COW_OLD_OR_NEW_OR_FAIL_CLOSED_ONLY\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"zenovfs-audit-fault-test: "<<error.what()<<"\n"; return 1;
    }
}
