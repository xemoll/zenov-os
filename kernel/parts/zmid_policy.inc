#include "../../security/zmid_crypto_material.hpp"

namespace zmid {
constexpr uint32_t schema_version = 1U;
constexpr uint32_t engine_version = 0x00000101U;
constexpr uint32_t compiled_database_floor = 1U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t max_database_bytes = 16U * 1024U;
constexpr uint32_t max_records = 32U;
constexpr uint32_t max_pattern_bytes = 32U;
constexpr uint8_t record_hash = 1U, record_pattern = 2U;
constexpr uint8_t action_block = 1U, action_quarantine = 2U, action_audit = 4U;
constexpr uint8_t known_actions = action_block | action_quarantine | action_audit;

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema, header_size;
    uint32_t database_version, minimum_engine, record_count, payload_size;
    uint8_t payload_sha256[32], key_id[8];
};
struct [[gnu::packed]] Record {
    uint8_t type, action;
    uint16_t value_length;
    uint8_t value[32];
    char name[48];
    uint8_t reserved[12];
};
static_assert(sizeof(Header) == 64U && sizeof(Record) == 96U);

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, sequence, engine, key, records };
Record active_records[max_records]{};
Record candidate_records[max_records]{};
uint32_t active_record_count = 0U, active_database_version = 0U, persistent_database_version = 0U;
uint32_t hash_records = 0U, pattern_records = 0U, audit_only_records = 0U;
bool ready = false;

bool canonical_name(const char* value, uint32_t capacity) {
    if (!value || !capacity) return false;
    uint32_t length = 0U;
    while (length < capacity && value[length]) ++length;
    if (!length || length == capacity) return false;
    for (uint32_t i = length + 1U; i < capacity; ++i) if (value[i] != 0) return false;
    return true;
}

bool record_valid(const Record& record) {
    if ((record.type != record_hash && record.type != record_pattern) || !record.action || (record.action & ~known_actions) != 0U ||
        !canonical_name(record.name, sizeof(record.name))) return false;
    if ((record.type == record_hash && record.value_length != 32U) ||
        (record.type == record_pattern && (!record.value_length || record.value_length > max_pattern_bytes))) return false;
    for (uint32_t i = record.value_length; i < sizeof(record.value); ++i) if (record.value[i] != 0U) return false;
    for (uint8_t byte : record.reserved) if (byte != 0U) return false;
    return true;
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version,
                    Record output[max_records], uint32_t& output_count, uint32_t& version) {
    output_count = version = 0U;
    if (!data || size < sizeof(Header) + signature_bytes || size > max_database_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    const char magic[4] = {'Z','M','I','D'};
    if (!storage::bytes_equal(header->magic, magic, 4U) || header->schema != schema_version || header->header_size != sizeof(Header) ||
        !header->record_count || header->record_count > max_records || header->payload_size != header->record_count * sizeof(Record) ||
        sizeof(Header) + header->payload_size + signature_bytes != size) return Validation::malformed;
    if (header->minimum_engine > engine_version) return Validation::engine;
    if (!storage::bytes_equal(header->key_id, kZmidRootKeyId, sizeof(header->key_id))) return Validation::key;
    uint8_t digest[32];
    security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!zgdb::rsa_pss_verify_with_modulus(data + size - signature_bytes, digest, kZmidRootModulus)) return Validation::signature;
    if (header->database_version < minimum_version) return Validation::rollback;
    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    for (uint32_t i = 0U; i < header->record_count; ++i) {
        if (!record_valid(records[i])) return Validation::records;
        for (uint32_t previous = 0U; previous < i; ++previous) {
            if (records[i].type == records[previous].type && records[i].value_length == records[previous].value_length &&
                storage::bytes_equal(records[i].value, records[previous].value, records[i].value_length)) return Validation::records;
        }
        output[i] = records[i];
    }
    output_count = header->record_count;
    version = header->database_version;
    return Validation::okay;
}

uint32_t parse_version(const uint8_t* data, uint32_t size) {
    uint32_t value = 0U;
    if (!data || !size) return 0U;
    for (uint32_t i = 0U; i < size; ++i) {
        if (data[i] == '\n' && i + 1U == size) break;
        if (data[i] < '0' || data[i] > '9' || value > 429496729U) return 0U;
        value = value * 10U + static_cast<uint32_t>(data[i] - '0');
    }
    return value;
}

bool write_version(uint32_t version) {
    char text[12]{};
    uint32_t count = 0U, value = version;
    char reverse[10]{};
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    for (uint32_t i = 0U; i < count; ++i) text[i] = reverse[count - 1U - i];
    text[count++] = '\n';
    return storage::write_file("/security/zenovguard-intelligence.version", reinterpret_cast<const uint8_t*>(text), count, false);
}

void activate(const Record source[max_records], uint32_t count, uint32_t version) {
    memset(active_records, 0, sizeof(active_records));
    hash_records = pattern_records = audit_only_records = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        active_records[i] = source[i];
        if (source[i].type == record_hash) ++hash_records; else ++pattern_records;
        if ((source[i].action & action_audit) && !(source[i].action & action_block)) ++audit_only_records;
    }
    active_record_count = count;
    active_database_version = version;
}

bool read_database(const char* path, uint32_t minimum_version, Validation& validation, uint32_t& version, uint32_t& count) {
    validation = Validation::malformed;
    version = count = 0U;
    uint32_t size = 0U;
    if (!storage::read_file(path, process::application_buffer, max_database_bytes, size)) return false;
    validation = validate(process::application_buffer, size, minimum_version, candidate_records, count, version);
    return validation == Validation::okay;
}

bool pattern_matches(const uint8_t* data, uint32_t size, const uint8_t* pattern, uint32_t pattern_size) {
    if (!data || !pattern || !pattern_size || pattern_size > size) return false;
    const uint32_t last = size - pattern_size;
    for (uint32_t offset = 0U; offset <= last; ++offset) {
        if (data[offset] != pattern[0]) continue;
        if (storage::bytes_equal(data + offset, pattern, pattern_size)) return true;
    }
    return false;
}

bool classify(const char* path, const uint8_t* data, uint32_t size, const uint8_t digest[32], security_guard::ScanResult& result) {
    if (!ready || (!data && size) || !digest) return false;
    for (uint32_t i = 0U; i < active_record_count; ++i) {
        const Record& record = active_records[i];
        const bool matched = record.type == record_hash
            ? security_guard::digest_equal(digest, record.value)
            : pattern_matches(data, size, record.value, record.value_length);
        if (!matched) continue;
        result.policy_action = record.action;
        result.verdict = (record.action & action_block) ? security_guard::Verdict::infected : security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, record.name, sizeof(result.signature));
        if (!result.path[0]) storage::path_copy(result.path, path ? path : "<memory>", sizeof(result.path));
        return true;
    }
    return false;
}

const char* validation_name(Validation validation) {
    switch (validation) {
        case Validation::digest: return "payload-digest";
        case Validation::signature: return "pss-signature";
        case Validation::rollback: return "rollback";
        case Validation::sequence: return "non-sequential";
        case Validation::engine: return "engine";
        case Validation::key: return "unknown-key";
        case Validation::records: return "records";
        default: return "malformed";
    }
}

bool update(const char* candidate_path) {
    Validation validation{};
    uint32_t version = 0U, count = 0U;
    if (!candidate_path || !*candidate_path || !read_database(candidate_path, active_database_version + 1U, validation, version, count)) {
        if (validation == Validation::rollback) serial::write("ZMID_ROLLBACK_REJECTED reason=");
        else if (validation == Validation::key) serial::write("ZMID_KEY_REJECTED reason=");
        else serial::write("ZMID_TAMPER_REJECTED reason=");
        serial::line(validation_name(validation));
        return false;
    }
    if (version != active_database_version + 1U) {
        serial::line("ZMID_SEQUENCE_REJECTED reason=non-sequential");
        return false;
    }
    uint32_t size = 0U;
    if (!storage::read_file(candidate_path, process::application_buffer, max_database_bytes, size)) return false;
    uint32_t final_version = 0U, final_count = 0U;
    validation = validate(process::application_buffer, size, active_database_version + 1U, candidate_records, final_count, final_version);
    if (validation != Validation::okay || final_version != version || final_count != count) return false;
    if (!storage::write_file("/security/zenovguard-intelligence.zmid", process::application_buffer, size, false)) return false;
    if (!write_version(version)) return false;
    activate(candidate_records, count, version);
    persistent_database_version = version;
    ready = true;
    security_guard::ScanResult event{};
    event.verdict = security_guard::Verdict::trusted;
    event.policy_action = action_audit;
    storage::path_copy(event.path, "/security/zenovguard-intelligence.zmid", sizeof(event.path));
    storage::path_copy(event.signature, "Signed.Intelligence.Update", sizeof(event.signature));
    security_guard::sha256(process::application_buffer, size, event.digest);
    event.size = size;
    if (!security_guard::record(security_guard::AuditAction::intelligence_update, event)) return false;
    serial::write("ZMID_ATOMIC_UPDATE_OK version="); process::serial_u32(version); serial::write("\r\n");
    return true;
}

bool self_test() {
    if (!ready || active_record_count < 5U || hash_records < 2U || pattern_records < 3U) return false;
    static constexpr char threat[] = "prefix-ZENOV_RANSOMWARE_TEST_V1-suffix";
    static constexpr char audit[] = "ZENOV_PUA_TEST_V1";
    static constexpr char clean[] = "ZENOV_ZMID_CLEAN_VECTOR_V1";
    security_guard::ScanResult result{};
    uint8_t digest[32];
    security_guard::sha256(reinterpret_cast<const uint8_t*>(threat), sizeof(threat) - 1U, digest);
    if (!classify("<zmid-selftest-threat>", reinterpret_cast<const uint8_t*>(threat), sizeof(threat) - 1U, digest, result) ||
        result.verdict != security_guard::Verdict::infected || !(result.policy_action & action_quarantine)) return false;
    result = {};
    security_guard::sha256(reinterpret_cast<const uint8_t*>(audit), sizeof(audit) - 1U, digest);
    if (!classify("<zmid-selftest-audit>", reinterpret_cast<const uint8_t*>(audit), sizeof(audit) - 1U, digest, result) ||
        result.verdict != security_guard::Verdict::suspicious || (result.policy_action & action_block)) return false;
    result = {};
    security_guard::sha256(reinterpret_cast<const uint8_t*>(clean), sizeof(clean) - 1U, digest);
    return !classify("<zmid-selftest-clean>", reinterpret_cast<const uint8_t*>(clean), sizeof(clean) - 1U, digest, result);
}

bool init() {
    uint8_t version_buffer[16];
    uint32_t version_size = 0U;
    persistent_database_version = storage::read_file("/security/zenovguard-intelligence.version", version_buffer, sizeof(version_buffer), version_size)
        ? parse_version(version_buffer, version_size) : 0U;
    uint32_t minimum = compiled_database_floor;
    if (persistent_database_version > minimum) minimum = persistent_database_version;
    Validation validation{};
    uint32_t version = 0U, count = 0U;
    if (!read_database("/security/zenovguard-intelligence.zmid", minimum, validation, version, count)) {
        serial::write("ZMID_INIT_FAILED reason="); serial::line(validation_name(validation)); ready = false; return false;
    }
    activate(candidate_records, count, version);
    if (persistent_database_version < version && !write_version(version)) { ready = false; return false; }
    persistent_database_version = version;
    ready = true;
    if (!self_test()) { ready = false; serial::line("ZMID_SELFTEST_FAILED"); return false; }
    serial::line("ZMID_ROOT_KEY_OK id=6ca6a5275544c533");
    serial::line("ZMID_PSS_SIGNATURE_OK");
    serial::write("ZMID_DATABASE_VERSION_OK version="); process::serial_u32(version); serial::write("\r\n");
    serial::write("ZMID_RULES_OK total="); process::serial_u32(active_record_count);
    serial::write(" hashes="); process::serial_u32(hash_records);
    serial::write(" patterns="); process::serial_u32(pattern_records);
    serial::write(" audit_only="); process::serial_u32(audit_only_records); serial::write("\r\n");
    serial::line("ZMID_READY");
    return true;
}

void print_status() {
    console::write("  Intelligence  "); console::line(ready ? "signature-valid / enforced" : "unavailable / writes and execution locked");
    console::write("  Database      "); console::unsigned_dec(active_database_version); console::put('\n');
    console::write("  Floor         "); console::unsigned_dec(compiled_database_floor); console::put('\n');
    console::write("  Hash rules    "); console::unsigned_dec(hash_records); console::put('\n');
    console::write("  Pattern rules "); console::unsigned_dec(pattern_records); console::put('\n');
    console::write("  Audit-only    "); console::unsigned_dec(audit_only_records); console::put('\n');
    console::line("  Root ID       6ca6a5275544c533");
    console::line("  Limits        32 rules / 32-byte patterns / 64 KiB files");
    serial::line("ZMID_STATUS_OK");
}
} // namespace zmid
