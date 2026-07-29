#include "../../security/zvrt_crypto_material.hpp"

namespace zvrt {
constexpr uint32_t schema_version = 1U;
constexpr uint32_t engine_version = 0x00000101U;
constexpr uint32_t compiled_manifest_floor = 1U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t max_manifest_bytes = 8U * 1024U;
constexpr uint32_t chunk_bytes = 4096U;
constexpr uint32_t max_records = 16U;
constexpr uint32_t max_leaves = process::application_buffer_bytes / chunk_bytes;
constexpr uint32_t flag_required = 1U, flag_immutable = 2U, known_record_flags = flag_required | flag_immutable;
constexpr uint32_t manifest_flags = 1U;
static_assert(max_leaves == 16U);

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema, header_size;
    uint32_t manifest_version, minimum_engine, record_count, payload_size;
    uint32_t chunk_size, flags;
    uint8_t payload_sha256[32], key_id[8], reserved[8];
};
struct [[gnu::packed]] Record {
    char path[48];
    uint32_t file_size, chunk_size, leaf_count, flags;
    uint8_t merkle_root[32], file_sha256[32];
};
static_assert(sizeof(Header) == 80U && sizeof(Record) == 128U);

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, engine, key, records };
constexpr uintptr_t record_workspace_address = supervisor_layout::zvrt_records;
constexpr uint32_t record_workspace_bytes = 2U * max_records * sizeof(Record);
static_assert(record_workspace_bytes == supervisor_layout::zvrt_records_bytes);
static_assert(record_workspace_address + record_workspace_bytes <= supervisor_layout::pmm_bitmap);
Record* active_records = reinterpret_cast<Record*>(record_workspace_address);
Record* candidate_records = active_records + max_records;
uint32_t active_record_count = 0U, active_manifest_version = 0U, persistent_manifest_version = 0U;
uint32_t verified_reads = 0U, blocked_reads = 0U, total_leaves = 0U, multichunk_records = 0U;
bool ready = false;

bool canonical_path(const char* value, uint32_t capacity) {
    return security_policy_format::canonical_absolute_path(value, capacity, false);
}

bool digest_nonzero(const uint8_t value[32]) {
    uint8_t combined = 0U;
    for (uint32_t i = 0U; i < 32U; ++i) combined = static_cast<uint8_t>(combined | value[i]);
    return combined != 0U;
}

bool record_valid(const Record& record) {
    if (!canonical_path(record.path, sizeof(record.path)) || !record.file_size ||
        record.file_size > process::application_buffer_bytes || record.chunk_size != chunk_bytes ||
        !record.leaf_count || record.leaf_count > max_leaves ||
        record.leaf_count != (record.file_size + chunk_bytes - 1U) / chunk_bytes ||
        record.flags != known_record_flags || !digest_nonzero(record.merkle_root) || !digest_nonzero(record.file_sha256)) return false;
    return true;
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version,
                    Record output[max_records], uint32_t& output_count, uint32_t& version) {
    output_count = version = 0U;
    if (!data || size < sizeof(Header) + sizeof(Record) + signature_bytes || size > max_manifest_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    const char magic[4] = {'Z','V','R','T'};
    if (!storage::bytes_equal(header->magic, magic, 4U) || header->schema != schema_version ||
        header->header_size != sizeof(Header) || !header->record_count || header->record_count > max_records ||
        header->payload_size != header->record_count * sizeof(Record) || header->chunk_size != chunk_bytes ||
        header->flags != manifest_flags || sizeof(Header) + header->payload_size + signature_bytes != size) return Validation::malformed;
    if (header->minimum_engine > engine_version) return Validation::engine;
    if (!storage::bytes_equal(header->key_id, kZvrtRootKeyId, sizeof(header->key_id))) return Validation::key;
    for (uint8_t byte : header->reserved) if (byte != 0U) return Validation::malformed;
    uint8_t digest[32];
    security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!zgdb::rsa_pss_verify_with_modulus(data + size - signature_bytes, digest, kZvrtRootModulus)) return Validation::signature;
    if (header->manifest_version < minimum_version) return Validation::rollback;

    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    for (uint32_t i = 0U; i < header->record_count; ++i) {
        if (!record_valid(records[i])) return Validation::records;
        for (uint32_t previous = 0U; previous < i; ++previous) {
            if (string_equal(records[i].path, records[previous].path)) return Validation::records;
        }
        output[i] = records[i];
    }
    output_count = header->record_count;
    version = header->manifest_version;
    return Validation::okay;
}

bool parse_version(const uint8_t* data, uint32_t size, uint32_t& output) {
    return security_policy_format::parse_nonzero_decimal_u32(data, size, output);
}

bool write_version(uint32_t version) {
    char text[12]{};
    char reverse[10]{};
    uint32_t count = 0U, value = version;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    for (uint32_t i = 0U; i < count; ++i) text[i] = reverse[count - 1U - i];
    text[count++] = '\n';
    return storage::write_file("/security/verified-reads.version", reinterpret_cast<const uint8_t*>(text), count, false) &&
        storage::sync_metadata();
}

void activate(const Record source[max_records], uint32_t count, uint32_t version) {
    memset(active_records, 0, max_records * sizeof(Record));
    total_leaves = multichunk_records = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        active_records[i] = source[i];
        total_leaves += source[i].leaf_count;
        if (source[i].leaf_count > 1U) ++multichunk_records;
    }
    active_record_count = count;
    active_manifest_version = version;
}

const Record* find_record(const char* input, char normalized[48]) {
    if (!input || !*input || !storage::normalize_path(input, normalized)) return nullptr;
    for (uint32_t i = 0U; i < active_record_count; ++i) if (string_equal(normalized, active_records[i].path)) return &active_records[i];
    return nullptr;
}

bool is_protected(const char* input) {
    char normalized[48]{};
    return find_record(input, normalized) != nullptr;
}

void store_le32(uint8_t output[4], uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
    output[2] = static_cast<uint8_t>(value >> 16U);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

void leaf_digest(const char* path, uint32_t file_size, uint32_t index,
                 const uint8_t* data, uint32_t size, uint8_t output[32]) {
    security_guard::Sha256Context context{};
    security_guard::sha256_init(context);
    const uint8_t domain = 0U;
    const uint8_t path_length = static_cast<uint8_t>(string_length(path));
    uint8_t encoded[4];
    security_guard::sha256_update(context, &domain, 1U);
    security_guard::sha256_update(context, &path_length, 1U);
    security_guard::sha256_update(context, reinterpret_cast<const uint8_t*>(path), path_length);
    store_le32(encoded, file_size); security_guard::sha256_update(context, encoded, sizeof(encoded));
    store_le32(encoded, index); security_guard::sha256_update(context, encoded, sizeof(encoded));
    store_le32(encoded, size); security_guard::sha256_update(context, encoded, sizeof(encoded));
    security_guard::sha256_update(context, data, size);
    security_guard::sha256_final(context, output);
}

void parent_digest(const uint8_t left[32], const uint8_t right[32], uint8_t output[32]) {
    security_guard::Sha256Context context{};
    security_guard::sha256_init(context);
    const uint8_t domain = 1U;
    security_guard::sha256_update(context, &domain, 1U);
    security_guard::sha256_update(context, left, 32U);
    security_guard::sha256_update(context, right, 32U);
    security_guard::sha256_final(context, output);
}

bool compute_root(const char* path, const uint8_t* data, uint32_t size, uint32_t expected_leaves, uint8_t output[32]) {
    if (!path || (!data && size) || !size || !expected_leaves || expected_leaves > max_leaves ||
        expected_leaves != (size + chunk_bytes - 1U) / chunk_bytes) return false;
    uint8_t nodes[max_leaves][32]{};
    for (uint32_t index = 0U; index < expected_leaves; ++index) {
        const uint32_t offset = index * chunk_bytes;
        const uint32_t remaining = size - offset;
        const uint32_t length = remaining < chunk_bytes ? remaining : chunk_bytes;
        leaf_digest(path, size, index, data + offset, length, nodes[index]);
    }
    uint32_t count = expected_leaves;
    while (count > 1U) {
        const uint32_t next = (count + 1U) / 2U;
        for (uint32_t i = 0U; i < next; ++i) {
            const uint32_t left = i * 2U;
            const uint32_t right = left + 1U < count ? left + 1U : left;
            uint8_t digest[32];
            parent_digest(nodes[left], nodes[right], digest);
            memcpy(nodes[i], digest, sizeof(digest));
        }
        count = next;
    }
    memcpy(output, nodes[0], 32U);
    return true;
}

bool record_failure(const char* normalized, const char* reason, const uint8_t* data, uint32_t size) {
    ++blocked_reads;
    security_guard::ScanResult event{};
    event.verdict = security_guard::Verdict::untrusted;
    event.size = size;
    storage::path_copy(event.path, normalized ? normalized : "<invalid>", sizeof(event.path));
    storage::path_copy(event.signature, reason ? reason : "ZVRT.Error", sizeof(event.signature));
    if (data || !size) security_guard::sha256(data, size, event.digest);
    const bool durable = security_guard::record(security_guard::AuditAction::read_block, event);
    serial::write("ZVRT_READ_BLOCKED path="); serial::write(event.path);
    serial::write(" reason="); serial::line(reason ? reason : "error");
    return durable;
}

bool verify_read(const char* input, const uint8_t* data, uint32_t size) {
    char normalized[48]{};
    const Record* record = find_record(input, normalized);
    if (!record) return true;
    if (!ready) { (void)record_failure(normalized, "policy-not-ready", data, size); return false; }
    if (!data || size != record->file_size) { (void)record_failure(normalized, "size", data, size); return false; }
    uint8_t digest[32];
    security_guard::sha256(data, size, digest);
    if (!security_guard::digest_equal(digest, record->file_sha256)) {
        (void)record_failure(normalized, "file-digest", data, size); return false;
    }
    uint8_t root[32];
    if (!compute_root(normalized, data, size, record->leaf_count, root) ||
        !security_guard::digest_equal(root, record->merkle_root)) {
        (void)record_failure(normalized, "merkle-root", data, size); return false;
    }
    ++verified_reads;
    serial::write("ZVRT_READ_OK path="); serial::write(normalized);
    serial::write(" chunks="); process::serial_u32(record->leaf_count); serial::write("\r\n");
    return true;
}

bool missing_read(const char* input, storage::FsStatus status) {
    char normalized[48]{};
    const Record* record = find_record(input, normalized);
    if (!record) return true;
    const char* reason = status == storage::FsStatus::checksum_mismatch ? "zenovfs-checksum" : "missing";
    (void)record_failure(normalized, reason, nullptr, 0U);
    return false;
}

const char* validation_name(Validation validation) {
    switch (validation) {
        case Validation::digest: return "payload-digest";
        case Validation::signature: return "pss-signature";
        case Validation::rollback: return "rollback";
        case Validation::engine: return "engine";
        case Validation::key: return "unknown-key";
        case Validation::records: return "records";
        default: return "malformed";
    }
}

bool self_test() {
    if (!ready || active_record_count != 4U || total_leaves != 5U || multichunk_records != 1U) return false;
    static constexpr char clean[] = "ZENOV_ZMID_CLEAN_VECTOR_V1\n";
    char normalized[48]{};
    const Record* record = find_record("/data/samples/clean.txt", normalized);
    if (!record || record->file_size != sizeof(clean) - 1U || record->leaf_count != 1U) return false;
    uint8_t root[32], digest[32];
    security_guard::sha256(reinterpret_cast<const uint8_t*>(clean), sizeof(clean) - 1U, digest);
    if (!security_guard::digest_equal(digest, record->file_sha256) ||
        !compute_root(normalized, reinterpret_cast<const uint8_t*>(clean), sizeof(clean) - 1U, 1U, root) ||
        !security_guard::digest_equal(root, record->merkle_root)) return false;
    return !is_protected("/data/samples/clean.txt.extra") && is_protected("/data/apps/fileio.elf");
}

bool init() {
    uint8_t version_buffer[16]{}; uint32_t version_size = 0U;
    if (!storage::read_file("/security/verified-reads.version", version_buffer, sizeof(version_buffer), version_size) ||
        !parse_version(version_buffer, version_size, persistent_manifest_version)) {
        serial::line("ZVRT_INIT_FAILED reason=version-state"); ready = false; return false;
    }
    uint32_t minimum = compiled_manifest_floor;
    if (persistent_manifest_version > minimum) minimum = persistent_manifest_version;

    uint32_t size = 0U;
    if (!storage::read_file("/security/verified-reads.zvrt", process::application_buffer, max_manifest_bytes, size)) {
        serial::line("ZVRT_INIT_FAILED reason=read"); ready = false; return false;
    }
    uint32_t version = 0U, count = 0U;
    const Validation validation = validate(process::application_buffer, size, minimum, candidate_records, count, version);
    if (validation != Validation::okay) {
        serial::write("ZVRT_INIT_FAILED reason="); serial::line(validation_name(validation)); ready = false; return false;
    }
    activate(candidate_records, count, version);
    ready = true;
    if (persistent_manifest_version < version && !write_version(version)) {
        serial::line("ZVRT_INIT_FAILED reason=version-persist"); ready = false; return false;
    }
    persistent_manifest_version = version;
    if (!self_test()) { serial::line("ZVRT_SELFTEST_FAILED"); ready = false; return false; }
    serial::line("ZVRT_ROOT_KEY_OK id=d28215ec62269ffc");
    serial::line("ZVRT_WORKSPACE_OK address=0x00310000 bytes=4096 supervisor-only=yes");
    serial::line("ZVRT_PSS_SIGNATURE_OK");
    serial::write("ZVRT_MANIFEST_OK version="); process::serial_u32(active_manifest_version);
    serial::write(" records="); process::serial_u32(active_record_count);
    serial::write(" chunk="); process::serial_u32(chunk_bytes);
    serial::write(" leaves="); process::serial_u32(total_leaves); serial::write("\r\n");
    serial::line("ZVRT_READY");
    return true;
}
} // namespace zvrt
