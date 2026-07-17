#include "../../security/zenpkg_crypto_material.hpp"

namespace package_catalog {

constexpr uint32_t schema_version = 2U;
constexpr uint32_t engine_version = 0x00000102U;
constexpr uint32_t compiled_catalog_floor = 1U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t maximum_catalog_bytes = 16U * 1024U;
constexpr uint32_t maximum_records = 16U;
constexpr const char* active_path = "/security/zenpkg.zpc";
constexpr const char* version_path = "/security/zenpkg.version";

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema;
    uint16_t header_size;
    uint32_t catalog_version;
    uint32_t minimum_engine;
    uint32_t record_count;
    uint32_t payload_size;
    uint8_t payload_sha256[32];
    uint8_t key_id[8];
};

struct [[gnu::packed]] Record {
    char name[32];
    char version[16];
    char entrypoint[48];
    uint32_t package_size;
    uint32_t payload_size;
    uint8_t payload_type;
    uint8_t flags;
    uint16_t reserved;
    uint8_t package_sha256[32];
    uint8_t payload_sha256[32];
};

static_assert(sizeof(Header) == 64U);
static_assert(sizeof(Record) == 172U);

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, sequence, engine, key, records };

Record active_records[maximum_records]{};
Record candidate_records[maximum_records]{};
uint32_t active_record_count = 0, candidate_record_count = 0;
uint32_t active_catalog_version = 0, candidate_catalog_version = 0, persistent_catalog_version = 0;
bool ready = false;

bool bounded_text(const char* text, uint32_t capacity, bool lower_identifier) {
    if (!text || !capacity) return false;
    uint32_t length = 0;
    while (length < capacity && text[length]) ++length;
    if (!length || length >= capacity) return false;
    for (uint32_t i = length + 1U; i < capacity; ++i) if (text[i] != 0) return false;
    if (!lower_identifier) return true;
    for (uint32_t i = 0; i < length; ++i) {
        const char value = text[i];
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_')) return false;
    }
    return true;
}

int compare_text(const char* left, const char* right) {
    return string_compare(left, right);
}

bool record_valid(const Record& record) {
    if (!bounded_text(record.name, sizeof(record.name), true) || !bounded_text(record.version, sizeof(record.version), false) ||
        !bounded_text(record.entrypoint, sizeof(record.entrypoint), false)) return false;
    if (record.payload_type != native_package::payload_zex1 && record.payload_type != native_package::payload_elf32) return false;
    if (record.flags || record.reserved || !record.package_size || !record.payload_size ||
        record.package_size > native_package::maximum_package_size || record.payload_size > storage::file_capacity()) return false;
    native_package::Manifest manifest{};
    string_copy(manifest.name, record.name, sizeof(manifest.name));
    string_copy(manifest.version, record.version, sizeof(manifest.version));
    manifest.payload_type = record.payload_type;
    char expected[48]{};
    if (!native_package::expected_entrypoint(manifest, expected) || !string_equal(expected, record.entrypoint)) return false;
    return true;
}

bool record_less(const Record& left, const Record& right) {
    const int name = compare_text(left.name, right.name);
    if (name != 0) return name < 0;
    return compare_text(left.version, right.version) < 0;
}

void mgf1_sha256(const uint8_t seed[32], uint8_t* output, uint32_t size) {
    uint32_t offset = 0, counter = 0;
    while (offset < size) {
        const uint8_t counter_be[4] = {static_cast<uint8_t>(counter >> 24U), static_cast<uint8_t>(counter >> 16U), static_cast<uint8_t>(counter >> 8U), static_cast<uint8_t>(counter)};
        uint8_t block[32]; security_guard::Sha256Context context{};
        security_guard::sha256_init(context); security_guard::sha256_update(context, seed, 32U);
        security_guard::sha256_update(context, counter_be, 4U); security_guard::sha256_final(context, block);
        uint32_t copy = size - offset; if (copy > 32U) copy = 32U;
        memcpy(output + offset, block, copy); offset += copy; ++counter;
    }
}

bool rsa_pss_verify(const uint8_t signature[signature_bytes], const uint8_t digest[32]) {
    zgdb::BigInteger encoded_signature{}, modulus{}, decoded{};
    zgdb::big_from_be(encoded_signature, signature); zgdb::big_from_be(modulus, zenpkg_crypto::kZenpkgRootModulus);
    if ((zenpkg_crypto::kZenpkgRootModulus[0] & 0x80U) == 0 || zgdb::big_compare(encoded_signature, modulus) >= 0) return false;
    zgdb::big_exp_65537(decoded, encoded_signature, modulus);
    uint8_t encoded[256]; zgdb::big_to_be(decoded, encoded);
    constexpr uint32_t hash_bytes = 32U, salt_bytes = 32U, database_bytes = 223U, padding_bytes = 190U;
    if (encoded[255] != 0xBCU || (encoded[0] & 0x80U) != 0) return false;
    const uint8_t* encoded_hash = encoded + database_bytes;
    uint8_t mask[database_bytes], database_bytes_decoded[database_bytes];
    mgf1_sha256(encoded_hash, mask, database_bytes);
    for (uint32_t i = 0; i < database_bytes; ++i) database_bytes_decoded[i] = static_cast<uint8_t>(encoded[i] ^ mask[i]);
    database_bytes_decoded[0] = static_cast<uint8_t>(database_bytes_decoded[0] & 0x7FU);
    uint8_t malformed = 0;
    for (uint32_t i = 0; i < padding_bytes; ++i) malformed = static_cast<uint8_t>(malformed | database_bytes_decoded[i]);
    malformed = static_cast<uint8_t>(malformed | static_cast<uint8_t>(database_bytes_decoded[padding_bytes] ^ 0x01U));
    uint8_t message[8U + hash_bytes + salt_bytes]{};
    memcpy(message + 8U, digest, hash_bytes);
    memcpy(message + 8U + hash_bytes, database_bytes_decoded + padding_bytes + 1U, salt_bytes);
    uint8_t expected_hash[hash_bytes]; security_guard::sha256(message, sizeof(message), expected_hash);
    return malformed == 0 && security_guard::digest_equal(encoded_hash, expected_hash);
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version,
                    Record output[maximum_records], uint32_t& output_count, uint32_t& output_version) {
    output_count = output_version = 0;
    if (!data || size < sizeof(Header) + signature_bytes || size > maximum_catalog_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    const char magic[4] = {'Z','P','C','2'};
    if (!storage::bytes_equal(header->magic, magic, sizeof(magic)) || header->schema != schema_version ||
        header->header_size != sizeof(Header) || header->minimum_engine > engine_version || !header->record_count ||
        header->record_count > maximum_records || header->payload_size != header->record_count * sizeof(Record) ||
        sizeof(Header) + header->payload_size + signature_bytes != size) {
        return header->minimum_engine > engine_version ? Validation::engine : Validation::malformed;
    }
    if (!storage::bytes_equal(header->key_id, zenpkg_crypto::kZenpkgRootKeyId, sizeof(header->key_id))) return Validation::key;
    uint8_t digest[32];
    security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!rsa_pss_verify(data + size - signature_bytes, digest)) return Validation::signature;
    if (header->catalog_version < minimum_version) return Validation::rollback;
    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    for (uint32_t i = 0; i < header->record_count; ++i) {
        if (!record_valid(records[i])) return Validation::records;
        if (i && !record_less(records[i - 1U], records[i])) return Validation::records;
        for (uint32_t j = 0; j < i; ++j) {
            if (string_equal(records[i].entrypoint, records[j].entrypoint)) return Validation::records;
        }
        output[i] = records[i];
    }
    output_count = header->record_count;
    output_version = header->catalog_version;
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
    char text[16];
    uint32_t count = 0, cursor = value;
    do { text[count++] = static_cast<char>('0' + cursor % 10U); cursor /= 10U; } while (cursor && count < 14U);
    for (uint32_t left = 0, right = count - 1U; left < right; ++left, --right) {
        const char swap = text[left]; text[left] = text[right]; text[right] = swap;
    }
    text[count++] = '\n';
    return storage::write_file(version_path, reinterpret_cast<const uint8_t*>(text), count, false) && storage::sync_metadata();
}

void activate(const Record source[maximum_records], uint32_t count, uint32_t version) {
    memset(active_records, 0, sizeof(active_records));
    for (uint32_t i = 0; i < count; ++i) active_records[i] = source[i];
    active_record_count = count;
    active_catalog_version = version;
    ready = true;
}

bool read_catalog(const char* path, uint32_t minimum_version, Record output[maximum_records],
                  uint32_t& count, uint32_t& version, Validation& validation) {
    count = version = 0;
    if (!path || !*path || !process::application_buffer) { validation = Validation::malformed; return false; }
    uint32_t size = 0;
    if (!storage::read_file(path, process::application_buffer, maximum_catalog_bytes, size)) { validation = Validation::malformed; return false; }
    validation = validate(process::application_buffer, size, minimum_version, output, count, version);
    return validation == Validation::okay;
}

bool records_equal(const Record left[maximum_records], const Record right[maximum_records], uint32_t count) {
    return memcmp(left, right, count * sizeof(Record)) == 0;
}

const Record* find_in(const Record records[maximum_records], uint32_t count, const char* name, const char* version) {
    for (uint32_t i = 0; i < count; ++i) {
        if (string_equal(records[i].name, name) && string_equal(records[i].version, version)) return &records[i];
    }
    return nullptr;
}

const Record* find_active(const char* name, const char* version) {
    return ready ? find_in(active_records, active_record_count, name, version) : nullptr;
}

bool record_matches(const Record& record, const char* entrypoint, uint8_t payload_type, uint32_t package_size,
                    uint32_t payload_size, const uint8_t package_digest[32], const uint8_t payload_digest[32]) {
    return string_equal(record.entrypoint, entrypoint) && record.payload_type == payload_type &&
        record.package_size == package_size && record.payload_size == payload_size &&
        security_guard::digest_equal(record.package_sha256, package_digest) &&
        security_guard::digest_equal(record.payload_sha256, payload_digest);
}

bool authorizes_package(const native_package::VerifiedPackage& package, uint32_t package_size) {
    const Record* record = find_active(package.manifest.name, package.manifest.version);
    return record && record_matches(*record, package.manifest.entrypoint, package.manifest.payload_type,
        package_size, package.payload_size, package.package_digest, package.payload_digest);
}

bool candidate_authorizes(const char* name, const char* version, const char* entrypoint, uint8_t payload_type,
                          uint32_t package_size, uint32_t payload_size, const uint8_t package_digest[32], const uint8_t payload_digest[32]) {
    const Record* record = find_in(candidate_records, candidate_record_count, name, version);
    return record && record_matches(*record, entrypoint, payload_type, package_size, payload_size, package_digest, payload_digest);
}

bool prepare_candidate(const char* path) {
    Validation validation{};
    candidate_record_count = candidate_catalog_version = 0;
    const uint32_t minimum = active_catalog_version ? active_catalog_version + 1U : compiled_catalog_floor;
    if (!read_catalog(path, minimum, candidate_records, candidate_record_count, candidate_catalog_version, validation)) {
        serial::write(validation == Validation::rollback ? "ZENPKG_CATALOG_ROLLBACK_REJECTED reason=" : "ZENPKG_CATALOG_TAMPER_REJECTED reason=");
        serial::line(validation == Validation::signature ? "signature" : validation == Validation::digest ? "payload-digest" :
            validation == Validation::rollback ? "rollback" : validation == Validation::sequence ? "non-sequential" :
            validation == Validation::engine ? "engine" : validation == Validation::key ? "unknown-key" :
            validation == Validation::records ? "records" : "malformed");
        return false;
    }
    if (active_catalog_version && candidate_catalog_version != active_catalog_version + 1U) {
        serial::line("ZENPKG_CATALOG_SEQUENCE_REJECTED reason=non-sequential");
        return false;
    }
    return true;
}

bool commit_candidate(const char* path) {
    Record reread[maximum_records]{};
    uint32_t reread_count = 0, reread_version = 0;
    Validation validation{};
    if (!read_catalog(path, active_catalog_version + 1U, reread, reread_count, reread_version, validation) ||
        reread_version != active_catalog_version + 1U || reread_count != candidate_record_count || reread_version != candidate_catalog_version ||
        !records_equal(reread, candidate_records, reread_count)) return false;
    uint32_t size = 0;
    if (!storage::read_file(path, process::application_buffer, maximum_catalog_bytes, size)) return false;
    if (!storage::write_file(active_path, process::application_buffer, size, false) || !storage::sync_metadata()) return false;
    if (!write_version(reread_version)) return false;
    activate(reread, reread_count, reread_version);
    persistent_catalog_version = reread_version;
    serial::write("ZENPKG_CATALOG_UPDATE_OK version="); process::serial_u32(reread_version); serial::write("\r\n");
    return true;
}

bool init() {
    ready = false;
    uint8_t version_buffer[16];
    uint32_t version_size = 0;
    persistent_catalog_version = storage::read_file(version_path, version_buffer, sizeof(version_buffer), version_size)
        ? parse_version(version_buffer, version_size) : 0;
    uint32_t minimum = compiled_catalog_floor;
    if (persistent_catalog_version > minimum) minimum = persistent_catalog_version;
    Record loaded[maximum_records]{};
    uint32_t count = 0, version = 0;
    Validation validation{};
    if (!read_catalog(active_path, minimum, loaded, count, version, validation)) {
        serial::line("ZENPKG_CATALOG_INIT_FAILED");
        return false;
    }
    activate(loaded, count, version);
    if (persistent_catalog_version < version && !write_version(version)) { ready = false; return false; }
    persistent_catalog_version = version;
    serial::line("ZENPKG_CATALOG_ROOT_KEY_OK id=30a8d71f32182651");
    serial::line("ZENPKG_CATALOG_PSS_SIGNATURE_OK");
    serial::write("ZENPKG_CATALOG_VERSION_OK version="); process::serial_u32(version); serial::write("\r\n");
    return true;
}

void print_status() {
    console::write("  Catalog       "); console::line(ready ? "signature-valid / enforced" : "unavailable / installs locked");
    console::write("  Version       "); console::unsigned_dec(active_catalog_version); console::put('\n');
    console::write("  Floor         "); console::unsigned_dec(compiled_catalog_floor); console::put('\n');
    console::write("  Records       "); console::unsigned_dec(active_record_count); console::put('\n');
    console::line("  Root ID       30a8d71f32182651");
    console::line("  Signature     RSA-2048 / PSS / SHA-256 / salt-32");
}

} // namespace package_catalog
