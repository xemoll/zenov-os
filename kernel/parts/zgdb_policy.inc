#include "../../security/zgdb_crypto_material.hpp"

namespace zgdb {
constexpr uint32_t schema_version = 1U;
constexpr uint32_t engine_version = 0x00000101U;
constexpr uint32_t compiled_policy_floor = 1U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t max_database_bytes = 16U * 1024U;
constexpr uint32_t max_records = 32U;
constexpr uint8_t record_trusted = 1U, record_threat = 2U, record_revoked = 3U;

struct [[gnu::packed]] Header {
    char magic[4]; uint16_t schema, header_size; uint32_t policy_version, minimum_engine, record_count, payload_size;
    uint8_t payload_sha256[32]; uint8_t reserved[8];
};
struct [[gnu::packed]] Record { uint8_t type, flags; uint16_t name_length; uint8_t digest[32]; char name[48]; };
static_assert(sizeof(Header) == 64 && sizeof(Record) == 84);

struct BigInteger { uint32_t limb[64]; };
Record active_records[max_records]{};
Record candidate_records[max_records]{};
uint32_t active_record_count = 0, active_policy_version = 0, persistent_policy_version = 0;
bool ready = false;

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, engine, trust };

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
void big_add_mod(BigInteger& output, const BigInteger& left, const BigInteger& right, const BigInteger& modulus) {
    unsigned long long carry = 0;
    for (uint32_t i = 0; i < 64U; ++i) { const unsigned long long sum = static_cast<unsigned long long>(left.limb[i]) + right.limb[i] + carry; output.limb[i] = static_cast<uint32_t>(sum); carry = sum >> 32U; }
    if (carry || big_compare(output, modulus) >= 0) big_subtract(output, modulus);
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

bool rsa_verify(const uint8_t signature[signature_bytes], const uint8_t digest[32]) {
    BigInteger encoded_signature{}, modulus{}, decoded{};
    big_from_be(encoded_signature, signature); big_from_be(modulus, kZgdbRootModulus);
    if (big_compare(encoded_signature, modulus) >= 0) return false;
    big_exp_65537(decoded, encoded_signature, modulus);
    uint8_t encoded[256]; big_to_be(decoded, encoded);
    static constexpr uint8_t digest_info[19] = {0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
    if (encoded[0] != 0 || encoded[1] != 1) return false;
    uint32_t position = 2, padding = 0;
    while (position < 256U && encoded[position] == 0xFFU) { ++position; ++padding; }
    if (padding < 8U || position >= 256U || encoded[position++] != 0 || position + sizeof(digest_info) + 32U != 256U) return false;
    if (memcmp(encoded + position, digest_info, sizeof(digest_info)) != 0) return false;
    return security_guard::digest_equal(encoded + position + sizeof(digest_info), digest);
}

bool record_name_valid(const Record& record) {
    if (!record.name_length || record.name_length >= sizeof(record.name) || record.name[record.name_length] != 0) return false;
    for (uint32_t i = record.name_length + 1U; i < sizeof(record.name); ++i) if (record.name[i] != 0) return false;
    return record.type >= record_trusted && record.type <= record_revoked && record.flags == 0;
}
bool compiled_trust_match(const Record& record) {
    if (record.type != record_trusted) return true;
    for (const auto& trusted : security_guard::trusted_executables)
        if (string_equal(record.name, trusted.path) && security_guard::digest_equal(record.digest, trusted.digest)) return true;
    return false;
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version, Record output[max_records], uint32_t& output_count, uint32_t& version) {
    output_count = version = 0;
    if (!data || size < sizeof(Header) + signature_bytes || size > max_database_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    const char magic[4] = {'Z','G','D','B'};
    if (!storage::bytes_equal(header->magic, magic, 4) || header->schema != schema_version || header->header_size != sizeof(Header) ||
        header->minimum_engine > engine_version || !header->record_count || header->record_count > max_records ||
        header->payload_size != header->record_count * sizeof(Record) || sizeof(Header) + header->payload_size + signature_bytes != size) return header->minimum_engine > engine_version ? Validation::engine : Validation::malformed;
    uint8_t digest[32]; security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!rsa_verify(data + size - signature_bytes, digest)) return Validation::signature;
    if (header->policy_version < minimum_version) return Validation::rollback;
    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    uint32_t trusted_count = 0;
    for (uint32_t i = 0; i < header->record_count; ++i) {
        if (!record_name_valid(records[i]) || !compiled_trust_match(records[i])) return Validation::trust;
        if (records[i].type == record_trusted) ++trusted_count;
        output[i] = records[i];
    }
    if (trusted_count != sizeof(security_guard::trusted_executables) / sizeof(security_guard::trusted_executables[0])) return Validation::trust;
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

bool read_database(const char* path, uint32_t minimum_version, Validation& validation, uint32_t& version) {
    if (!process::application_buffer) return false;
    uint32_t size = 0;
    if (!storage::read_file(path, process::application_buffer, max_database_bytes, size)) { validation = Validation::malformed; return false; }
    uint32_t count = 0; validation = validate(process::application_buffer, size, minimum_version, candidate_records, count, version);
    if (validation != Validation::okay) return false;
    active_record_count = count; return true;
}

const Record* matching_record(uint8_t type, const uint8_t digest[32]) {
    for (uint32_t i = 0; i < active_record_count; ++i) if (active_records[i].type == type && security_guard::digest_equal(active_records[i].digest, digest)) return &active_records[i];
    return nullptr;
}

bool precheck(const char* path, const uint8_t* data, uint32_t size) {
    if (!ready) { serial::line("ZGDB_POLICY_NOT_READY"); return false; }
    uint8_t digest[32]; security_guard::sha256(data, size, digest);
    if (const Record* revoked = matching_record(record_revoked, digest)) {
        serial::write("ZGDB_REVOCATION_BLOCKED path="); serial::write(path); serial::write(" rule="); serial::line(revoked->name); return false;
    }
    if (const Record* threat = matching_record(record_threat, digest)) {
        serial::write("ZENOV_GUARD_DETECTED path="); serial::write(path); serial::write(" signature="); serial::line(threat->name); (void)security_guard::quarantine_path(path); return false;
    }
    return true;
}

const char* validation_name(Validation validation) {
    switch (validation) { case Validation::digest: return "payload-digest"; case Validation::signature: return "signature"; case Validation::rollback: return "rollback"; case Validation::engine: return "engine"; case Validation::trust: return "trust-baseline"; default: return "malformed"; }
}

bool update(const char* candidate_path) {
    Validation validation{}; uint32_t version = 0;
    if (!candidate_path || !*candidate_path || !read_database(candidate_path, active_policy_version + 1U, validation, version)) {
        serial::write(validation == Validation::rollback ? "ZGDB_ROLLBACK_REJECTED reason=" : "ZGDB_TAMPER_REJECTED reason="); serial::line(validation_name(validation)); return false;
    }
    uint32_t size = 0;
    if (!storage::read_file(candidate_path, process::application_buffer, max_database_bytes, size)) return false;
    if (!storage::write_file("/security/zenovguard.zgdb", process::application_buffer, size, false)) return false;
    if (!write_version(version)) return false;
    uint32_t count = 0, verified_version = 0;
    validation = validate(process::application_buffer, size, version, candidate_records, count, verified_version);
    if (validation != Validation::okay || verified_version != version) return false;
    activate(candidate_records, count, version); persistent_policy_version = version; ready = true;
    serial::write("ZGDB_ATOMIC_UPDATE_OK version="); process::serial_u32(version); serial::write("\r\n"); return true;
}

bool init() {
    uint8_t version_buffer[16]; uint32_t version_size = 0;
    persistent_policy_version = storage::read_file("/security/zenovguard.version", version_buffer, sizeof(version_buffer), version_size) ? parse_version(version_buffer, version_size) : 0;
    uint32_t minimum = compiled_policy_floor; if (persistent_policy_version > minimum) minimum = persistent_policy_version;
    Validation validation{}; uint32_t version = 0;
    if (!read_database("/security/zenovguard.zgdb", minimum, validation, version)) { serial::write("ZGDB_INIT_FAILED reason="); serial::line(validation_name(validation)); ready = false; return false; }
    const uint32_t count = active_record_count; activate(candidate_records, count, version);
    if (persistent_policy_version < version && !write_version(version)) { ready = false; return false; }
    persistent_policy_version = version; ready = true;
    serial::line("ZGDB_SIGNATURE_OK"); serial::write("ZGDB_POLICY_VERSION_OK version="); process::serial_u32(version); serial::write("\r\n"); serial::line("ZGDB_READY"); return true;
}

void print_status() {
    console::write("  Database      "); console::line(ready ? "signature-valid / enforced" : "unavailable / execution locked");
    console::write("  Policy        "); console::unsigned_dec(active_policy_version); console::put('\n');
    console::write("  Floor         "); console::unsigned_dec(compiled_policy_floor); console::put('\n');
    console::write("  Records       "); console::unsigned_dec(active_record_count); console::put('\n');
    console::line("  Signature     RSA-2048 / PKCS#1 v1.5 / SHA-256");
}
} // namespace zgdb
