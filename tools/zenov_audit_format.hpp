#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace zenov_audit_host {
constexpr std::uint16_t kCapacity = 64U;
constexpr std::size_t kJournalBytes = 8288U;

#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint16_t schema, header_size, record_size, capacity;
    std::uint32_t count, next_index, next_sequence;
    std::uint8_t anchor_hash[32], head_hash[32], reserved[8];
};
struct Record {
    std::uint32_t sequence, tick;
    std::uint8_t action, verdict;
    std::uint16_t path_length;
    char path[48];
    std::uint8_t digest[32], record_hash[32], reserved[4];
};
struct Journal { Header header; Record records[kCapacity]; };
#pragma pack(pop)

static_assert(sizeof(Header) == 96U);
static_assert(sizeof(Record) == 128U);
static_assert(sizeof(Journal) == kJournalBytes);

struct Sha256 {
    std::array<std::uint32_t, 8> state{};
    std::uint64_t total = 0;
    std::array<std::uint8_t, 64> block{};
    std::size_t used = 0;
};

inline constexpr std::array<std::uint32_t, 64> kSha256Constants = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U,
};

inline std::uint32_t rotate_right(std::uint32_t value, unsigned count) { return (value >> count) | (value << (32U - count)); }
inline std::uint32_t load_be32(const std::uint8_t* input) {
    return (static_cast<std::uint32_t>(input[0]) << 24U) | (static_cast<std::uint32_t>(input[1]) << 16U) |
        (static_cast<std::uint32_t>(input[2]) << 8U) | input[3];
}
inline void store_be32(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 24U);
    output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U);
    output[3] = static_cast<std::uint8_t>(value);
}
inline void transform(Sha256& context, const std::uint8_t* block) {
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
        const std::uint32_t temp1=h+s1+choice+kSha256Constants[i]+words[i];
        const std::uint32_t s0=rotate_right(a,2U)^rotate_right(a,13U)^rotate_right(a,22U);
        const std::uint32_t majority=(a&b)^(a&c)^(b&c);
        const std::uint32_t temp2=s0+majority;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    context.state[0]+=a; context.state[1]+=b; context.state[2]+=c; context.state[3]+=d;
    context.state[4]+=e; context.state[5]+=f; context.state[6]+=g; context.state[7]+=h;
}
inline void sha_init(Sha256& context) {
    context.state={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    context.total=0; context.used=0;
}
inline void sha_update(Sha256& context, const std::uint8_t* data, std::size_t size) {
    context.total += size;
    for (std::size_t i = 0; i < size; ++i) {
        context.block[context.used++] = data[i];
        if (context.used == context.block.size()) { transform(context, context.block.data()); context.used = 0; }
    }
}
inline std::array<std::uint8_t, 32> sha_final(Sha256& context) {
    const std::uint64_t bits=context.total*8U;
    context.block[context.used++]=0x80U;
    if (context.used>56U) {
        while(context.used<64U) context.block[context.used++]=0;
        transform(context,context.block.data()); context.used=0;
    }
    while(context.used<56U) context.block[context.used++]=0;
    for(std::size_t i=0;i<8U;++i) context.block[63U-i]=static_cast<std::uint8_t>(bits>>(i*8U));
    transform(context,context.block.data());
    std::array<std::uint8_t,32> output{};
    for(std::size_t i=0;i<8U;++i) store_be32(output.data()+i*4U,context.state[i]);
    return output;
}
inline std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size) {
    Sha256 context{}; sha_init(context); sha_update(context, data, size); return sha_final(context);
}
inline bool same_hash(const std::uint8_t* left, const std::uint8_t* right) {
    std::uint8_t difference=0;
    for(std::size_t i=0;i<32U;++i) difference=static_cast<std::uint8_t>(difference|(left[i]^right[i]));
    return difference==0;
}
inline bool sha256_self_test() {
    static constexpr std::uint8_t expected[32] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    static constexpr std::uint8_t input[3] = {'a','b','c'};
    const auto actual=sha256(input,sizeof(input)); return same_hash(actual.data(),expected);
}

inline std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash=2166136261U;
    for(std::size_t i=0;i<size;++i){hash^=data[i];hash*=16777619U;}
    return hash;
}
inline bool all_zero(const std::uint8_t* data, std::size_t size) {
    std::uint8_t value=0;
    for(std::size_t i=0;i<size;++i) value=static_cast<std::uint8_t>(value|data[i]);
    return value==0;
}
inline void store_le16(std::uint8_t* output,std::uint16_t value){output[0]=static_cast<std::uint8_t>(value);output[1]=static_cast<std::uint8_t>(value>>8U);}
inline void store_le32(std::uint8_t* output,std::uint32_t value){
    output[0]=static_cast<std::uint8_t>(value);output[1]=static_cast<std::uint8_t>(value>>8U);
    output[2]=static_cast<std::uint8_t>(value>>16U);output[3]=static_cast<std::uint8_t>(value>>24U);
}
inline std::array<std::uint8_t,32> record_hash(const Record& record,const std::uint8_t previous[32]){
    constexpr std::array<std::uint8_t,16> domain={'Z','E','N','O','V','-','A','U','D','I','T','-','V','1',0,0};
    std::array<std::uint8_t,12> metadata{};
    store_le32(metadata.data(),record.sequence);store_le32(metadata.data()+4U,record.tick);
    metadata[8]=record.action;metadata[9]=record.verdict;store_le16(metadata.data()+10U,record.path_length);
    Sha256 context{};sha_init(context);sha_update(context,domain.data(),domain.size());sha_update(context,previous,32U);
    sha_update(context,metadata.data(),metadata.size());sha_update(context,reinterpret_cast<const std::uint8_t*>(record.path),sizeof(record.path));
    sha_update(context,record.digest,sizeof(record.digest));return sha_final(context);
}
inline void initialize_empty(Journal& journal) {
    std::memset(&journal,0,sizeof(journal));std::memcpy(journal.header.magic,"ZGAL",4U);
    journal.header.schema=1U;journal.header.header_size=sizeof(Header);journal.header.record_size=sizeof(Record);
    journal.header.capacity=kCapacity;journal.header.next_sequence=1U;
}
inline bool verify(const Journal& journal,bool require_nonempty=false) {
    const Header& h=journal.header;
    if(std::memcmp(h.magic,"ZGAL",4U)!=0||h.schema!=1U||h.header_size!=sizeof(Header)||h.record_size!=sizeof(Record)||
       h.capacity!=kCapacity||h.count>kCapacity||h.next_index>=kCapacity||!h.next_sequence||!all_zero(h.reserved,sizeof(h.reserved))) return false;
    if(require_nonempty&&!h.count)return false;
    if(!h.count)return h.next_index==0U&&h.next_sequence==1U&&all_zero(h.anchor_hash,32U)&&all_zero(h.head_hash,32U);
    if(h.next_sequence<=h.count)return false;
    if(h.count<kCapacity&&(h.next_index!=h.count||!all_zero(h.anchor_hash,32U)))return false;
    const std::uint32_t first=h.count==kCapacity?h.next_index:0U;
    const std::uint32_t first_sequence=h.next_sequence-h.count;
    std::array<std::uint8_t,32> previous{};std::memcpy(previous.data(),h.anchor_hash,32U);
    for(std::uint32_t i=0;i<h.count;++i){
        const Record& record=journal.records[(first+i)%kCapacity];
        if(record.sequence!=first_sequence+i||record.action>9U||record.verdict>5U||!record.path_length||record.path_length>=sizeof(record.path)||
           record.path[record.path_length]!=0||!all_zero(record.reserved,sizeof(record.reserved)))return false;
        for(std::size_t p=record.path_length+1U;p<sizeof(record.path);++p)if(record.path[p]!=0)return false;
        const auto computed=record_hash(record,previous.data());if(!same_hash(computed.data(),record.record_hash))return false;previous=computed;
    }
    return same_hash(previous.data(),h.head_hash);
}
} // namespace zenov_audit_host
