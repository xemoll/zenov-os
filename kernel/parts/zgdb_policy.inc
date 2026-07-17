#include "../../security/zgdb_crypto_material.hpp"

namespace zgdb {
constexpr uint32_t schema_version = 2U;
constexpr uint32_t engine_version = 0x00000102U;
constexpr uint32_t compiled_policy_floor = 3U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t max_database_bytes = 16U * 1024U;
constexpr uint32_t max_records = 32U;
constexpr uint8_t record_trusted = 1U, record_threat = 2U, record_revoked = 3U;

struct [[gnu::packed]] Header {
    char magic[4]; uint16_t schema, header_size; uint32_t policy_version, minimum_engine, record_count, payload_size;
    uint8_t payload_sha256[32]; uint8_t key_id[8];
};
struct [[gnu::packed]] Record { uint8_t type, flags; uint16_t name_length; uint8_t digest[32]; char name[48]; };
static_assert(sizeof(Header) == 64 && sizeof(Record) == 84);

struct BigInteger { uint32_t limb[64]; };
Record active_records[max_records]{};
Record candidate_records[max_records]{};
uint32_t active_record_count = 0, active_policy_version = 0, persistent_policy_version = 0;
bool ready = false;

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, sequence, engine, key, trust };

void big_zero(BigInteger& value) { memset(value.limb, 0, sizeof(value.limb)); }
void big_from_be(BigInteger& value, const uint8_t input[256]) {
    big_zero(value);
    for (uint32_t i = 0; i < 256U; ++i) { const uint32_t reverse = 255U - i; value.limb[reverse / 4U] |= static_cast<uint32_t>(input[i]) << ((reverse % 4U) * 8U); }
}
void big_to_be(const BigInteger& value, uint8_t output[256]) {
    for (uint32_t i = 0; i < 256U; ++i) { const uint32_t reverse = 255U - i; output[i] = static_cast<uint8_t>(value.limb[reverse / 4U] >> ((reverse % 4U) * 8U)); }
}
int32_t big_compare(const BigInteger& left, const BigInteger& right) {
    for (uint32_t i = 64U; i > 0; --i) { const uint32_t index = i - 1U; if (left.limb[index] < right.limb[index]) return -1; if (left.limb[index] > right.limb[index]) return 1; }
    return 0;
}
void big_subtract(BigInteger& left, const BigInteger& right) {
    unsigned long long borrow = 0;
    for (uint32_t i = 0; i < 64U; ++i) {
        const unsigned long long current = left.limb[i], subtrahend = static_cast<unsigned long long>(right.limb[i]) + borrow;
        left.limb[i] = static_cast<uint32_t>(current - subtrahend); borrow = current < subtrahend ? 1ULL : 0ULL;
    }
}
void big_add_plain(BigInteger& output, const BigInteger& left, const BigInteger& right) {
    unsigned long long carry = 0;
    for (uint32_t i = 0; i < 64U; ++i) { const unsigned long long sum = static_cast<unsigned long long>(left.limb[i]) + right.limb[i] + carry; output.limb[i] = static_cast<uint32_t>(sum); carry = sum >> 32U; }
}
void big_add_mod(BigInteger& output, const BigInteger& left, const BigInteger& right, const BigInteger& modulus) {
    BigInteger threshold = modulus;
    big_subtract(threshold, right);
    if (big_compare(left, threshold) >= 0) { output = left; big_subtract(output, threshold); }
    else big_add_plain(output, left, right);
}
bool big_bit(const BigInteger& value, uint32_t bit) { return ((value.limb[bit / 32U] >> (bit % 32U)) & 1U) != 0; }
void big_multiply_mod(BigInteger& output, const BigInteger& left, const BigInteger& right, const BigInteger& modulus) {
    BigInteger result{}, current = left, temporary{};
    for (uint32_t bit = 0; bit < 2048U; ++bit) {
        if (big_bit(right, bit)) { big_add_mod(temporary, result, current, modulus); result = temporary; }
        big_add_mod(temporary, current, current, modulus); current = temporary;
    }
    output = result;
}
void big_exp_65537(BigInteger& output, const BigInteger& signature, const BigInteger& modulus) {
    BigInteger result{}, temporary{}; result.limb[0] = 1U;
    constexpr uint32_t exponent = 65537U;
    for (int32_t bit = 16; bit >= 0; --bit) {
        big_multiply_mod(temporary, result, result, modulus); result = temporary;
        if ((exponent >> static_cast<uint32_t>(bit)) & 1U) { big_multiply_mod(temporary, result, signature, modulus); result = temporary; }
    }
    output = result;
}

void mgf1_sha256(const uint8_t seed[32], uint8_t* output, uint32_t size) {
    uint32_t offset = 0, counter = 0;
    while (offset < size) {
        uint8_t counter_be[4] = {static_cast<uint8_t>(counter >> 24U), static_cast<uint8_t>(counter >> 16U), static_cast<uint8_t>(counter >> 8U), static_cast<uint8_t>(counter)};
        uint8_t block[32]; security_guard::Sha256Context context{};
        security_guard::sha256_init(context); security_guard::sha256_update(context, seed, 32U); security_guard::sha256_update(context, counter_be, 4U); security_guard::sha256_final(context, block);
        uint32_t copy = size - offset; if (copy > 32U) copy = 32U;
        memcpy(output + offset, block, copy); offset += copy; ++counter;
    }
}

bool rsa_pss_verify(const uint8_t signature[signature_bytes], const uint8_t digest[32]) {
    BigInteger encoded_signature{}, modulus{}, decoded{};
    big_from_be(encoded_signature, signature); big_from_be(modulus, kZgdbRootModulus);
    if ((kZgdbRootModulus[0] & 0x80U) == 0 || big_compare(encoded_signature, modulus) >= 0) return false;
    big_exp_65537(decoded, encoded_signature, modulus);
    uint8_t encoded[256]; big_to_be(decoded, encoded);
    constexpr uint32_t hash_bytes = 32U, salt_bytes = 32U, database_bytes = 223U, padding_bytes = 190U;
    if (encoded[255] != 0xBCU || (encoded[0] & 0x80U) != 0) return false;
    const uint8_t* encoded_hash = encoded + database_bytes;
    uint8_t mask[database_bytes], database[database_bytes]; mgf1_sha256(encoded_hash, mask, database_bytes);
    for (uint32_t i = 0; i < database_bytes; ++i) database[i] = static_cast<uint8_t>(encoded[i] ^ mask[i]);
    database[0] = static_cast<uint8_t>(database[0] & 0x7FU);
    uint8_t malformed = 0;
    for (uint32_t i = 0; i < padding_bytes; ++i) malformed = static_cast<uint8_t>(malformed | database[i]);
    malformed = static_cast<uint8_t>(malformed | static_cast<uint8_t>(database[padding_bytes] ^ 0x01U));
    uint8_t message[8U + hash_bytes + salt_bytes]{};
    memcpy(message + 8U, digest, hash_bytes); memcpy(message + 8U + hash_bytes, database + padding_bytes + 1U, salt_bytes);
    uint8_t expected_hash[hash_bytes]; security_guard::sha256(message, sizeof(message), expected_hash);
    return malformed == 0 && security_guard::digest_equal(encoded_hash, expected_hash);
}

bool record_name_valid(const Record& record) {
    if (!record.name_length || record.name_length >= sizeof(record.name) || record.name[record.name_length] != 0) return false;
    for (uint32_t i = record.name_length + 1U; i < sizeof(record.name); ++i) if (record.name[i] != 0) return false;
    return record.type >= record_trusted && record.type <= record_revoked && record.flags == 0;
}
int32_t compiled_trust_index(const Record& record) {
    if (record.type != record_trusted) return -1;
    const uint32_t count = sizeof(security_guard::trusted_executables) / sizeof(security_guard::trusted_executables[0]);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& trusted = security_guard::trusted_executables[i];
        if (string_equal(record.name, trusted.path) && security_guard::digest_equal(record.digest, trusted.digest)) return static_cast<int32_t>(i);
    }
    return -1;
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version, Record output[max_records], uint32_t& output_count, uint32_t& version) {
    output_count = version = 0;
    if (!data || size < sizeof(Header) + signature_bytes || size > max_database_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    const char magic[4] = {'Z','G','D','B'};
    if (!storage::bytes_equal(header->magic, magic, 4) || header->schema != schema_version || header->header_size != sizeof(Header) ||
        header->minimum_engine > engine_version || !header->record_count || header->record_count > max_records ||
        header->payload_size != header->record_count * sizeof(Record) || sizeof(Header) + header->payload_size + signature_bytes != size) return header->minimum_engine > engine_version ? Validation::engine : Validation::malformed;
    if (!storage::bytes_equal(header->key_id, kZgdbRootKeyId, sizeof(header->key_id))) return Validation::key;
    uint8_t digest[32]; security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!rsa_pss_verify(data + size - signature_bytes, digest)) return Validation::signature;
    if (header->policy_version < minimum_version) return Validation::rollback;
    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    constexpr uint32_t trusted_total = sizeof(security_guard::trusted_executables) / sizeof(security_guard::trusted_executables[0]);
    bool trusted_seen[trusted_total]{};
    for (uint32_t i = 0; i < header->record_count; ++i) {
        if (!record_name_valid(records[i])) return Validation::trust;
        if (records[i].type == record_trusted) {
            const int32_t index = compiled_trust_index(records[i]);
            if (index < 0 || trusted_seen[static_cast<uint32_t>(index)]) return Validation::trust;
            trusted_seen[static_cast<uint32_t>(index)] = true;
        }
        output[i] = records[i];
    }
    for (bool present : trusted_seen) if (!present) return Validation::trust;
    output_count = header->record_count; version = header->policy_version; return Validation::okay;
}

uint32_t parse_version(const uint8_t* data, uint32_t size) {
    uint32_t value = 0; if (!data || !size) return 0;
    for (uint32_t i = 0; i < size; ++i) { if (data[i] == '\n' || data[i] == 0) break; if (data[i] < '0' || data[i] > '9' || value > 1000000U) return 0; value = value * 10U + static_cast<uint32_t>(data[i] - '0'); }
    return value;
}
bool write_version(uint32_t value) {
    char text[16]; uint32_t count = 0, cursor = value; do { text[count++] = static_cast<char>('0' + cursor % 10U); cursor /= 10U; } while (cursor && count < 14U);
    for (uint32_t left = 0, right = count - 1U; left < right; ++left, --right) { const char swap = text[left]; text[left] = text[right]; text[right] = swap; }
    text[count++] = '\n';
    return storage::write_file("/security/zenovguard.version", reinterpret_cast<const uint8_t*>(text), count, false);
}

void activate(const Record source[max_records], uint32_t count, uint32_t version) {
    memset(active_records, 0, sizeof(active_records));
    for (uint32_t i = 0; i < count; ++i) active_records[i] = source[i];
    active_record_count = count; active_policy_version = version;
}

bool read_database(const char* path, uint32_t minimum_version, Validation& validation, uint32_t& version, uint32_t& count) {
    count = version = 0;
    if (!process::application_buffer) return false;
    uint32_t size = 0;
    if (!storage::read_file(path, process::application_buffer, max_database_bytes, size)) { validation = Validation::malformed; return false; }
    validation = validate(process::application_buffer, size, minimum_version, candidate_records, count, version);
    return validation == Validation::okay;
}

const Record* matching_record(uint8_t type, const uint8_t digest[32]) {
    for (uint32_t i = 0; i < active_record_count; ++i) if (active_records[i].type == type && security_guard::digest_equal(active_records[i].digest, digest)) return &active_records[i];
    return nullptr;
}
void build_result(security_guard::ScanResult& result, const char* path, const uint8_t digest[32], security_guard::Verdict verdict, const char* signature, uint32_t size) {
    result.verdict = verdict; result.size = size; result.executable = true; memcpy(result.digest, digest, 32);
    char normalized[48];
    if (path && storage::normalize_path(path, normalized)) storage::path_copy(result.path, normalized, sizeof(result.path));
    else storage::path_copy(result.path, path ? path : "<memory>", sizeof(result.path));
    storage::path_copy(result.signature, signature, sizeof(result.signature));
}
bool quarantine_detected(const security_guard::ScanResult& result) {
    if (!security_guard::ensure_quarantine_directory() || security_guard::starts_with_quarantine(result.path)) return false;
    char destination[48]; security_guard::build_quarantine_path(result.digest, destination);
    if (!storage::rename_entry(result.path, destination)) return false;
    ++security_guard::quarantined; security_guard::record(security_guard::AuditAction::quarantine, result);
    security_guard::serial_decision("ZENOV_GUARD_QUARANTINE_OK", result); return true;
}

bool precheck(const char* path, const uint8_t* data, uint32_t size) {
    if (!ready) { serial::line("ZGDB_POLICY_NOT_READY"); return false; }
    uint8_t digest[32]; security_guard::sha256(data, size, digest);
    if (const Record* revoked = matching_record(record_revoked, digest)) {
        security_guard::ScanResult result{}; build_result(result, path, digest, security_guard::Verdict::untrusted, revoked->name, size);
        security_guard::record(security_guard::AuditAction::execute, result);
        serial::write("ZGDB_REVOCATION_BLOCKED path="); serial::write(result.path); serial::write(" rule="); serial::line(revoked->name); return false;
    }
    if (const Record* threat = matching_record(record_threat, digest)) {
        security_guard::ScanResult result{}; build_result(result, path, digest, security_guard::Verdict::infected, threat->name, size);
        security_guard::record(security_guard::AuditAction::execute, result); security_guard::serial_decision("ZENOV_GUARD_DETECTED", result);
        (void)quarantine_detected(result); return false;
    }
    return true;
}

const char* validation_name(Validation validation) {
    switch (validation) {
        case Validation::digest: return "payload-digest"; case Validation::signature: return "pss-signature";
        case Validation::rollback: return "rollback"; case Validation::sequence: return "non-sequential";
        case Validation::engine: return "engine"; case Validation::key: return "unknown-key";
        case Validation::trust: return "trust-baseline"; default: return "malformed";
    }
}

bool update(const char* candidate_path) {
    Validation validation{}; uint32_t version = 0, count = 0;
    if (!candidate_path || !*candidate_path || !read_database(candidate_path, active_policy_version + 1U, validation, version, count)) {
        if (validation == Validation::rollback) serial::write("ZGDB_ROLLBACK_REJECTED reason=");
        else if (validation == Validation::key) serial::write("ZGDB_KEY_REJECTED reason=");
        else serial::write("ZGDB_TAMPER_REJECTED reason=");
        serial::line(validation_name(validation)); return false;
    }
    if (version != active_policy_version + 1U) { serial::line("ZGDB_SEQUENCE_REJECTED reason=non-sequential"); return false; }
    uint32_t size = 0;
    if (!storage::read_file(candidate_path, process::application_buffer, max_database_bytes, size)) return false;
    uint32_t final_count = 0, final_version = 0;
    validation = validate(process::application_buffer, size, active_policy_version + 1U, candidate_records, final_count, final_version);
    if (validation != Validation::okay || final_version != version || final_count != count) return false;
    if (!storage::write_file("/security/zenovguard.zgdb", process::application_buffer, size, false)) return false;
    if (!write_version(version)) return false;
    activate(candidate_records, count, version); persistent_policy_version = version; ready = true;
    serial::write("ZGDB_ATOMIC_UPDATE_OK version="); process::serial_u32(version); serial::write("\r\n"); return true;
}

bool init() {
    uint8_t version_buffer[16]; uint32_t version_size = 0;
    persistent_policy_version = storage::read_file("/security/zenovguard.version", version_buffer, sizeof(version_buffer), version_size) ? parse_version(version_buffer, version_size) : 0;
    uint32_t minimum = compiled_policy_floor; if (persistent_policy_version > minimum) minimum = persistent_policy_version;
    Validation validation{}; uint32_t version = 0, count = 0;
    if (!read_database("/security/zenovguard.zgdb", minimum, validation, version, count)) { serial::write("ZGDB_INIT_FAILED reason="); serial::line(validation_name(validation)); ready = false; return false; }
    activate(candidate_records, count, version);
    if (persistent_policy_version < version && !write_version(version)) { ready = false; return false; }
    persistent_policy_version = version; ready = true;
    serial::line("ZGDB_ROOT_KEY_OK id=6f788074c018f5aa"); serial::line("ZGDB_PSS_SIGNATURE_OK");
    serial::write("ZGDB_POLICY_VERSION_OK version="); process::serial_u32(version); serial::write("\r\n"); serial::line("ZGDB_READY"); return true;
}

void print_status() {
    console::write("  Database      "); console::line(ready ? "signature-valid / enforced" : "unavailable / execution locked");
    console::write("  Policy        "); console::unsigned_dec(active_policy_version); console::put('\n');
    console::write("  Floor         "); console::unsigned_dec(compiled_policy_floor); console::put('\n');
    console::write("  Records       "); console::unsigned_dec(active_record_count); console::put('\n');
    console::line("  Root ID       6f788074c018f5aa");
    console::line("  Signature     RSA-2048 / PSS / SHA-256 / salt-32");
}
} // namespace zgdb
