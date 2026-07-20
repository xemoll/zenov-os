#include "../../security/zcap_crypto_material.hpp"

namespace zcap {
constexpr uint32_t schema_version = 1U;
constexpr uint32_t engine_version = 0x00000101U;
constexpr uint32_t compiled_policy_floor = 1U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t max_policy_bytes = 8U * 1024U;
constexpr uint32_t profile_count = 7U;
constexpr uint32_t capability_all = 0xFFU;

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema, header_size;
    uint32_t policy_version, minimum_engine, record_count, payload_size;
    uint8_t payload_sha256[32], key_id[8];
};
struct [[gnu::packed]] Record {
    char path[48];
    uint32_t mask;
    char read_scope[48];
    char write_scope[48];
    uint8_t reserved[12];
};
static_assert(sizeof(Header) == 64U && sizeof(Record) == 160U);

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, sequence, engine, key, trust };
Record active_records[profile_count]{};
Record candidate_records[profile_count]{};
uint32_t active_policy_version = 0, persistent_policy_version = 0;
bool ready = false;

bool canonical_string(const char* value, uint32_t capacity, bool allow_empty) {
    if (!value || !capacity) return false;
    uint32_t length = 0;
    while (length < capacity && value[length]) ++length;
    if (length == capacity || (!allow_empty && length == 0U)) return false;
    for (uint32_t i = length + 1U; i < capacity; ++i) if (value[i] != 0) return false;
    if (length && value[0] != '/') return false;
    return true;
}

const Record* find(const char* normalized_path) {
    if (!ready || !normalized_path) return nullptr;
    for (uint32_t i = 0; i < profile_count; ++i) if (string_equal(active_records[i].path, normalized_path)) return &active_records[i];
    return nullptr;
}

bool exact_trusted_bijection(const Record records[profile_count]) {
    constexpr uint32_t trusted_count = sizeof(security_guard::trusted_executables) / sizeof(security_guard::trusted_executables[0]);
    if (trusted_count != profile_count) return false;
    bool trusted_seen[trusted_count]{};
    for (uint32_t i = 0; i < profile_count; ++i) {
        const Record& record = records[i];
        if (!canonical_string(record.path, sizeof(record.path), false) ||
            !canonical_string(record.read_scope, sizeof(record.read_scope), true) ||
            !canonical_string(record.write_scope, sizeof(record.write_scope), true) ||
            (record.mask & ~capability_all) != 0U) return false;
        for (uint8_t byte : record.reserved) if (byte != 0U) return false;
        int32_t match = -1;
        for (uint32_t trusted = 0; trusted < trusted_count; ++trusted) {
            if (string_equal(record.path, security_guard::trusted_executables[trusted].path)) {
                if (match >= 0) return false;
                match = static_cast<int32_t>(trusted);
            }
        }
        if (match < 0 || trusted_seen[static_cast<uint32_t>(match)]) return false;
        trusted_seen[static_cast<uint32_t>(match)] = true;
        const bool needs_read_scope = (record.mask & ((1U << 2U) | (1U << 4U))) != 0U;
        const bool needs_write_scope = (record.mask & (1U << 3U)) != 0U;
        if (needs_read_scope != (record.read_scope[0] != 0) || needs_write_scope != (record.write_scope[0] != 0)) return false;
        if (record.read_scope[0]) {
            char normalized[48];
            if (!storage::normalize_path(record.read_scope, normalized) || !string_equal(normalized, record.read_scope)) return false;
        }
        if (record.write_scope[0]) {
            char normalized[48];
            if (!storage::normalize_path(record.write_scope, normalized) || !string_equal(normalized, record.write_scope)) return false;
        }
    }
    for (bool present : trusted_seen) if (!present) return false;
    return true;
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version, Record output[profile_count], uint32_t& version) {
    version = 0;
    if (!data || size < sizeof(Header) + signature_bytes || size > max_policy_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    const char magic[4] = {'Z','C','A','P'};
    if (!storage::bytes_equal(header->magic, magic, 4U) || header->schema != schema_version || header->header_size != sizeof(Header) ||
        header->minimum_engine > engine_version || header->record_count != profile_count ||
        header->payload_size != profile_count * sizeof(Record) || sizeof(Header) + header->payload_size + signature_bytes != size) {
        return header->minimum_engine > engine_version ? Validation::engine : Validation::malformed;
    }
    if (!storage::bytes_equal(header->key_id, kZcapRootKeyId, sizeof(header->key_id))) return Validation::key;
    uint8_t digest[32];
    security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!zgdb::rsa_pss_verify_with_modulus(data + size - signature_bytes, digest, kZcapRootModulus)) return Validation::signature;
    if (header->policy_version < minimum_version) return Validation::rollback;
    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    for (uint32_t i = 0; i < profile_count; ++i) output[i] = records[i];
    if (!exact_trusted_bijection(output)) return Validation::trust;
    version = header->policy_version;
    return Validation::okay;
}

uint32_t parse_version(const uint8_t* data, uint32_t size) {
    uint32_t value = 0;
    if (!data || !size) return 0;
    for (uint32_t i = 0; i < size; ++i) {
        if (data[i] == '\n' || data[i] == 0) break;
        if (data[i] < '0' || data[i] > '9' || value > 1000000U) return 0;
        value = value * 10U + static_cast<uint32_t>(data[i] - '0');
    }
    return value;
}

bool write_version(uint32_t value) {
    char text[16]; uint32_t count = 0, cursor = value;
    do { text[count++] = static_cast<char>('0' + cursor % 10U); cursor /= 10U; } while (cursor && count < 14U);
    for (uint32_t left = 0, right = count - 1U; left < right; ++left, --right) {
        const char swap = text[left]; text[left] = text[right]; text[right] = swap;
    }
    text[count++] = '\n';
    return storage::write_file("/security/syscall-capabilities.version", reinterpret_cast<const uint8_t*>(text), count, false);
}

void activate(const Record source[profile_count], uint32_t version) {
    memset(active_records, 0, sizeof(active_records));
    for (uint32_t i = 0; i < profile_count; ++i) active_records[i] = source[i];
    active_policy_version = version;
}

bool read_policy(const char* path, uint32_t minimum_version, Validation& validation, uint32_t& version) {
    version = 0;
    if (!process::application_buffer) { validation = Validation::malformed; return false; }
    uint32_t size = 0;
    if (!storage::read_file(path, process::application_buffer, max_policy_bytes, size)) { validation = Validation::malformed; return false; }
    validation = validate(process::application_buffer, size, minimum_version, candidate_records, version);
    return validation == Validation::okay;
}

const char* validation_name(Validation validation) {
    switch (validation) {
        case Validation::digest: return "payload-digest";
        case Validation::signature: return "pss-signature";
        case Validation::rollback: return "rollback";
        case Validation::sequence: return "non-sequential";
        case Validation::engine: return "engine";
        case Validation::key: return "unknown-key";
        case Validation::trust: return "trust-bijection";
        default: return "malformed";
    }
}

bool update(const char* candidate_path) {
    Validation validation{}; uint32_t version = 0;
    if (!candidate_path || !*candidate_path || !read_policy(candidate_path, active_policy_version + 1U, validation, version)) {
        if (validation == Validation::rollback) serial::write("ZCAP_ROLLBACK_REJECTED reason=");
        else if (validation == Validation::key) serial::write("ZCAP_KEY_REJECTED reason=");
        else serial::write("ZCAP_TAMPER_REJECTED reason=");
        serial::line(validation_name(validation));
        return false;
    }
    if (version != active_policy_version + 1U) {
        serial::line("ZCAP_SEQUENCE_REJECTED reason=non-sequential");
        return false;
    }
    uint32_t size = 0;
    if (!storage::read_file(candidate_path, process::application_buffer, max_policy_bytes, size)) return false;
    uint32_t final_version = 0;
    validation = validate(process::application_buffer, size, active_policy_version + 1U, candidate_records, final_version);
    if (validation != Validation::okay || final_version != version) return false;
    if (!storage::write_file("/security/syscall-capabilities.zcap", process::application_buffer, size, false)) return false;
    if (!write_version(version)) return false;
    activate(candidate_records, version);
    persistent_policy_version = version;
    ready = true;
    serial::write("ZCAP_ATOMIC_UPDATE_OK version="); process::serial_u32(version); serial::write("\r\n");
    return true;
}

bool self_test() {
    if (!ready || !exact_trusted_bijection(active_records)) return false;
    const Record* fileio = find("/apps/fileio.elf");
    const Record* args = find("/apps/args.elf");
    const Record* kaccess = find("/apps/kaccess.elf");
    return fileio && args && kaccess && fileio->mask == 0x7DU && args->mask == 0x05U && kaccess->mask == 0U;
}

bool init() {
    uint8_t version_buffer[16]; uint32_t version_size = 0;
    persistent_policy_version = storage::read_file("/security/syscall-capabilities.version", version_buffer, sizeof(version_buffer), version_size)
        ? parse_version(version_buffer, version_size) : 0U;
    uint32_t minimum = compiled_policy_floor;
    if (persistent_policy_version > minimum) minimum = persistent_policy_version;
    Validation validation{}; uint32_t version = 0;
    if (!read_policy("/security/syscall-capabilities.zcap", minimum, validation, version)) {
        serial::write("ZCAP_INIT_FAILED reason="); serial::line(validation_name(validation)); ready = false; return false;
    }
    activate(candidate_records, version);
    if (persistent_policy_version < version && !write_version(version)) { ready = false; return false; }
    persistent_policy_version = version;
    ready = true;
    serial::line("ZCAP_ROOT_KEY_OK id=9202c73fad96ad66");
    serial::line("ZCAP_PSS_SIGNATURE_OK");
    serial::write("ZCAP_POLICY_VERSION_OK version="); process::serial_u32(version); serial::write("\r\n");
    serial::line("ZCAP_READY");
    return true;
}

void print_status() {
    console::write("  Capability DB  "); console::line(ready ? "signature-valid / enforced" : "unavailable / execution locked");
    console::write("  Policy         "); console::unsigned_dec(active_policy_version); console::put('\n');
    console::write("  Floor          "); console::unsigned_dec(compiled_policy_floor); console::put('\n');
    console::write("  Profiles       "); console::unsigned_dec(profile_count); console::put('\n');
    console::line("  Root ID        9202c73fad96ad66");
    console::line("  Signature      RSA-2048 / PSS / SHA-256 / salt-32");
}
} // namespace zcap
