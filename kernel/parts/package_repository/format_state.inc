constexpr uint16_t schema_version = 1U;
constexpr uint16_t role_root = 1U, role_targets = 2U, role_snapshot = 3U, role_timestamp = 4U, role_delegated = 5U;
constexpr uint32_t maximum_metadata_bytes = 8U * 1024U;
constexpr uint32_t maximum_keys = 8U, maximum_roles = 8U, maximum_targets = 16U, maximum_signatures = 4U;
constexpr const char* state_path = "/var/lib/zenpkg/repo.v1";

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema, role;
    uint32_t version;
    unsigned long long expires;
    uint32_t body_size;
    uint16_t signature_count, threshold;
    uint8_t body_sha256[32], reserved[4];
};
struct [[gnu::packed]] Signature { uint8_t key_id[8], bytes[256]; };
struct [[gnu::packed]] RootHead { uint32_t consistent_snapshot, key_count, role_count, reserved; };
struct [[gnu::packed]] KeyRecord { uint8_t key_id[8], modulus[256]; };
struct [[gnu::packed]] RoleSpec { uint16_t role, threshold, key_count, reserved; uint8_t key_ids[16]; };
struct [[gnu::packed]] ReferenceHead { uint32_t meta_count, reserved; };
struct [[gnu::packed]] MetaReference { char name[32]; uint32_t version, length; uint8_t digest[32]; };
struct [[gnu::packed]] DelegationHead { uint32_t target_count, delegation_count; };
struct [[gnu::packed]] Delegation {
    char role_name[32], prefix[32];
    uint8_t key_id[8];
    uint16_t threshold;
    uint8_t terminating, reserved;
};
struct [[gnu::packed]] TargetsHead { uint32_t target_count, reserved; };
struct [[gnu::packed]] Target {
    char path[64], name[32], version[16], entrypoint[48], read_scope[48], write_scope[48];
    uint32_t package_length;
    uint8_t payload_type, reserved0[3];
    uint32_t syscall_mask;
    uint8_t package_digest[32], payload_digest[32];
};
struct [[gnu::packed]] State {
    char magic[8];
    uint32_t root, targets, delegated, snapshot, timestamp, checksum;
};
static_assert(sizeof(Header) == 64U && sizeof(Signature) == 264U && sizeof(KeyRecord) == 264U);
static_assert(sizeof(RoleSpec) == 24U && sizeof(MetaReference) == 72U && sizeof(Target) == 332U);

enum class Validation : uint8_t {
    okay, storage, malformed, digest, signature, threshold, expired, rollback, root_sequence,
    reference, delegation, target, state
};

struct Envelope {
    Header header{};
    uint8_t file_digest[32]{};
    uint32_t file_size = 0;
    const uint8_t* body = nullptr;
    const Signature* signatures = nullptr;
};
struct RootTrust {
    KeyRecord keys[maximum_keys]{};
    RoleSpec roles[maximum_roles]{};
    uint32_t key_count = 0, role_count = 0, version = 0;
};

uint8_t* metadata_buffer = nullptr;
RootTrust active_root{};
Target active_targets[maximum_targets]{};
uint32_t active_target_count = 0;
State persistent_state{};
bool ready = false;

bool fixed_text_valid(const char* text, uint32_t capacity, bool allow_empty = false) {
    if (!text || !capacity) return false;
    uint32_t end = 0;
    while (end < capacity && text[end]) {
        const uint8_t value = static_cast<uint8_t>(text[end]);
        if (value < 0x20U || value > 0x7EU) return false;
        ++end;
    }
    if ((!allow_empty && !end) || end == capacity) return false;
    for (uint32_t i = end + 1U; i < capacity; ++i) if (text[i]) return false;
    return true;
}
bool key_id_equal(const uint8_t left[8], const uint8_t right[8]) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 8U; ++i) difference = static_cast<uint8_t>(difference | static_cast<uint8_t>(left[i] ^ right[i]));
    return difference == 0;
}
const KeyRecord* find_key(const RootTrust& trust, const uint8_t key_id[8]) {
    for (uint32_t i = 0; i < trust.key_count; ++i) if (key_id_equal(trust.keys[i].key_id, key_id)) return &trust.keys[i];
    return nullptr;
}
const RoleSpec* find_role(const RootTrust& trust, uint16_t role) {
    for (uint32_t i = 0; i < trust.role_count; ++i) if (trust.roles[i].role == role) return &trust.roles[i];
    return nullptr;
}
uint32_t state_checksum(const State& state) {
    return storage::fnv1a(reinterpret_cast<const uint8_t*>(&state), sizeof(State) - sizeof(state.checksum));
}
bool state_valid(const State& state) {
    const char magic[8] = {'Z','R','S','T','1',0,0,0};
    return storage::bytes_equal(state.magic, magic, sizeof(magic)) && state.checksum == state_checksum(state);
}
void state_initialize(State& state) {
    memset(&state, 0, sizeof(state));
    const char magic[8] = {'Z','R','S','T','1',0,0,0};
    memcpy(state.magic, magic, sizeof(magic));
    state.checksum = state_checksum(state);
}
bool persist_state(const State& next) {
    State candidate = next;
    candidate.checksum = state_checksum(candidate);
    if (!storage::package_write_file(state_path, reinterpret_cast<const uint8_t*>(&candidate), sizeof(candidate), false)) return false;
    if (!storage::sync_metadata()) return false;
    uint32_t size = 0;
    State confirmed{};
    if (!storage::read_file(state_path, reinterpret_cast<uint8_t*>(&confirmed), sizeof(confirmed), size) ||
        size != sizeof(confirmed) || !state_valid(confirmed) || confirmed.checksum != candidate.checksum) return false;
    persistent_state = confirmed;
    return true;
}
Validation load_state() {
    storage::FileInfo info{};
    if (!storage::query(state_path, info)) {
        State empty{};
        state_initialize(empty);
        return persist_state(empty) ? Validation::okay : Validation::state;
    }
    if (info.type != storage::zfs_type_file || info.size != sizeof(State)) return Validation::state;
    uint32_t size = 0;
    if (!storage::read_file(state_path, reinterpret_cast<uint8_t*>(&persistent_state), sizeof(persistent_state), size) ||
        size != sizeof(persistent_state) || !state_valid(persistent_state)) return Validation::state;
    return Validation::okay;
}
