struct Header{char magic[4];std::uint16_t schema,role;std::uint32_t version;std::uint64_t expires;std::uint32_t body_size;std::uint16_t signature_count,threshold;std::uint8_t body_sha[32],reserved[4];};
struct Signature{std::uint8_t key_id[8],bytes[256];};
struct RootHead{std::uint32_t consistent_snapshot,key_count,role_count,reserved;};
struct KeyRecord{std::uint8_t key_id[8],modulus[256];};
struct RoleSpec{std::uint16_t role,threshold,key_count,reserved;std::uint8_t key_ids[16];};
struct DelegHead{std::uint32_t target_count,delegation_count;};
struct Delegation{char role_name[32],prefix[32];std::uint8_t key_id[8];std::uint16_t threshold;std::uint8_t terminating,reserved;};
struct TargetsHead{std::uint32_t target_count,reserved;};
struct Target{char path[64],name[32],version[16],entrypoint[48],read_scope[48],write_scope[48];std::uint32_t package_length;std::uint8_t payload_type,reserved0[3];std::uint32_t syscall_mask;std::uint8_t package_digest[32],payload_digest[32];};
struct SnapshotHead{std::uint32_t meta_count,reserved;};
struct Meta{char name[32];std::uint32_t version,length;std::uint8_t digest[32];};
struct State{char magic[8];std::uint32_t root,targets,delegated,snapshot,timestamp,checksum;};
#pragma pack(pop)
static_assert(sizeof(Header)==64&&sizeof(Signature)==264&&sizeof(KeyRecord)==264&&sizeof(RoleSpec)==24&&sizeof(Target)==332&&sizeof(Meta)==72);

enum Role:std::uint16_t{Root=1,Targets=2,Snapshot=3,Timestamp=4,Delegated=5};

Bytes read_all(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);if(!in)throw Error("cannot open "+p.string());in.seekg(0,std::ios::end);const auto n=in.tellg();if(n<0||n>65536)throw Error("invalid file size "+p.string());Bytes b(static_cast<std::size_t>(n));in.seekg(0);if(!b.empty())in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!in&& !b.empty())throw Error("short read "+p.string());return b;}
std::string text(const char*data,std::size_t cap){const auto*end=static_cast<const char*>(std::memchr(data,0,cap));if(!end)throw Error("unterminated metadata string");return std::string(data,end);}
std::string hex(const std::uint8_t*d,std::size_t n){static constexpr char h[]="0123456789abcdef";std::string s; s.reserve(n*2);for(std::size_t i=0;i<n;++i){s.push_back(h[d[i]>>4]);s.push_back(h[d[i]&15]);}return s;}
std::uint32_t fnv(const std::uint8_t*d,std::size_t n){std::uint32_t h=2166136261U;for(std::size_t i=0;i<n;++i){h^=d[i];h*=16777619U;}return h;}

struct Envelope{
    Bytes bytes; const Header*header=nullptr;const std::uint8_t*body=nullptr;const Signature*signatures=nullptr;
    explicit Envelope(Bytes b):bytes(std::move(b)){
        if(bytes.size()<sizeof(Header)+sizeof(Signature))throw Error("metadata truncated");
        header=reinterpret_cast<const Header*>(bytes.data());
        if(std::memcmp(header->magic,"ZRM1",4)||header->schema!=1||header->signature_count==0||header->signature_count>4||header->threshold==0||header->threshold>header->signature_count)throw Error("invalid metadata header");
        const std::size_t expected=sizeof(Header)+header->body_size+static_cast<std::size_t>(header->signature_count)*sizeof(Signature);
        if(expected!=bytes.size())throw Error("metadata length mismatch");
        body=bytes.data()+sizeof(Header);signatures=reinterpret_cast<const Signature*>(body+header->body_size);
        const auto digest=Sha256::hash(body,header->body_size);if(!equal_digest(digest.data(),header->body_sha))throw Error("metadata body digest mismatch");
        for(std::uint8_t b0:header->reserved)if(b0)throw Error("metadata reserved bits set");
    }
    Digest signed_digest()const{return Sha256::hash(bytes.data(),sizeof(Header)+header->body_size);}
};

struct RootTrust{std::vector<KeyRecord>keys;std::vector<RoleSpec>roles;std::uint32_t version=0;};
const KeyRecord* find_key(const RootTrust&r,const std::uint8_t id[8]){for(const auto&k:r.keys)if(std::memcmp(k.key_id,id,8)==0)return &k;return nullptr;}
const RoleSpec* find_role(const RootTrust&r,std::uint16_t role){for(const auto&s:r.roles)if(s.role==role)return &s;return nullptr;}
RootTrust parse_root(const Envelope&e){
    if(e.header->role!=Root||e.header->body_size<sizeof(RootHead))throw Error("not root metadata");
    const auto*h=reinterpret_cast<const RootHead*>(e.body);if(!h->consistent_snapshot||h->key_count==0||h->key_count>8||h->role_count==0||h->role_count>8||h->reserved)throw Error("invalid root body");
    const std::size_t need=sizeof(RootHead)+static_cast<std::size_t>(h->key_count)*sizeof(KeyRecord)+static_cast<std::size_t>(h->role_count)*sizeof(RoleSpec);
    if(need!=e.header->body_size)throw Error("root body length mismatch");
    RootTrust r;r.version=e.header->version;
    const auto*k=reinterpret_cast<const KeyRecord*>(e.body+sizeof(RootHead));r.keys.assign(k,k+h->key_count);
    const auto*s=reinterpret_cast<const RoleSpec*>(reinterpret_cast<const std::uint8_t*>(k+h->key_count));r.roles.assign(s,s+h->role_count);
    for(std::size_t i=0;i<r.keys.size();++i){if(std::all_of(std::begin(r.keys[i].key_id),std::end(r.keys[i].key_id),[](auto v){return v==0;}))throw Error("empty root key id");for(std::size_t j=0;j<i;++j)if(std::memcmp(r.keys[i].key_id,r.keys[j].key_id,8)==0)throw Error("duplicate root key id");}
    for(const auto&spec:r.roles){if(spec.threshold==0||spec.key_count==0||spec.threshold>spec.key_count||spec.key_count>2)throw Error("invalid role threshold");for(std::uint16_t i=0;i<spec.key_count;++i)if(!find_key(r,spec.key_ids+i*8))throw Error("role references unknown key");}
    return r;
}
bool verify_threshold(const Envelope&e,const RootTrust&r,std::uint16_t role){
    const auto*spec=find_role(r,role);if(!spec)return false;const auto digest=e.signed_digest();std::uint16_t good=0;std::uint8_t seen[2][8]{};
    for(std::uint16_t i=0;i<e.header->signature_count;++i){const auto&sig=e.signatures[i];bool allowed=false;for(std::uint16_t k=0;k<spec->key_count;++k)if(std::memcmp(sig.key_id,spec->key_ids+k*8,8)==0)allowed=true;if(!allowed)continue;bool duplicate=false;for(std::uint16_t n=0;n<good;++n)if(std::memcmp(seen[n],sig.key_id,8)==0)duplicate=true;if(duplicate)continue;const auto*key=find_key(r,sig.key_id);if(key&&verify_pss(sig.bytes,digest.data(),key->modulus)){std::memcpy(seen[good],sig.key_id,8);++good;}}
    return good>=spec->threshold;
}
RootTrust bootstrap_root(const Envelope&e){
    if(e.header->version!=1||e.header->role!=Root)throw Error("bootstrap root must be version 1");
    RootTrust compiled;KeyRecord k{};std::memcpy(k.key_id,kZenRepoBootstrapRootKeyId,8);std::memcpy(k.modulus,kZenRepoBootstrapRootModulus,256);compiled.keys.push_back(k);RoleSpec s{};s.role=Root;s.threshold=s.key_count=1;std::memcpy(s.key_ids,k.key_id,8);compiled.roles.push_back(s);
    if(!verify_threshold(e,compiled,Root))throw Error("bootstrap root signature invalid");
    return parse_root(e);
}
RootTrust update_root(const Envelope&e,const RootTrust&old){
    if(e.header->role!=Root||e.header->version!=old.version+1U)throw Error("root version is not sequential");
    const RootTrust next=parse_root(e);
    if(!verify_threshold(e,old,Root)||!verify_threshold(e,next,Root))throw Error("root rotation threshold failed");
    return next;
}
void verify_role(const Envelope&e,const RootTrust&r,std::uint16_t role,std::uint64_t now){if(e.header->role!=role)throw Error("metadata role mismatch");if(e.header->expires<=now)throw Error("metadata expired");if(!verify_threshold(e,r,role))throw Error("metadata signature threshold failed");}
const Meta* find_meta(const Envelope& e, const std::string& name) {
    if (e.header->body_size < sizeof(SnapshotHead)) throw Error("metadata reference body truncated");
    const auto* h = reinterpret_cast<const SnapshotHead*>(e.body);
    if (h->reserved || h->meta_count == 0 || h->meta_count > 8 ||
        sizeof(SnapshotHead) + h->meta_count * sizeof(Meta) != e.header->body_size) {
        throw Error("invalid metadata reference table");
    }
    const auto* metadata = reinterpret_cast<const Meta*>(e.body + sizeof(SnapshotHead));
    for (std::uint32_t i = 0; i < h->meta_count; ++i) {
        if (text(metadata[i].name, sizeof(metadata[i].name)) == name) return &metadata[i];
    }
    return nullptr;
}
void verify_reference(const Meta&m,const Envelope&e){if(m.version!=e.header->version||m.length!=e.bytes.size())throw Error("metadata version or length mismatch");const auto d=Sha256::hash(e.bytes);if(!equal_digest(d.data(),m.digest))throw Error("metadata mix-and-match digest mismatch");}

struct VerifiedSet{RootTrust root;Envelope timestamp,snapshot,targets,delegated;std::vector<Target>target_list;};
