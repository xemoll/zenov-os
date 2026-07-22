#include "../../security/zrwp_crypto_material.hpp"

namespace zrwp {
constexpr uint32_t schema_version = 1U;
constexpr uint32_t engine_version = 0x00000101U;
constexpr uint32_t compiled_policy_floor = 1U;
constexpr uint32_t signature_bytes = 256U;
constexpr uint32_t max_policy_bytes = 16U * 1024U;
constexpr uint32_t max_records = 16U;
constexpr uint8_t record_protected = 1U, record_writer = 2U;
constexpr uint8_t operation_write = 1U, operation_rename = 2U, operation_remove = 4U;
constexpr uint8_t known_operations = operation_write | operation_rename | operation_remove;
constexpr uint32_t mode_audit = 0U, mode_block = 1U;

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema, header_size;
    uint32_t policy_version, minimum_engine, mode, record_count, payload_size;
    uint32_t window_ticks, max_writes, max_renames, max_removes, max_bytes;
    uint8_t payload_sha256[32], key_id[8], reserved[8];
};
struct [[gnu::packed]] Record {
    uint8_t type, operations;
    uint16_t path_length;
    char path[48];
    uint8_t digest[32];
    uint8_t reserved[12];
};
static_assert(sizeof(Header) == 96U && sizeof(Record) == 96U);

enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, sequence, engine, key, records };
struct RuntimePolicy {
    uint32_t version = 0U, mode = mode_audit, record_count = 0U;
    uint32_t window_ticks = 0U, max_writes = 0U, max_renames = 0U, max_removes = 0U, max_bytes = 0U;
    Record records[max_records]{};
};
struct MutationWindow {
    bool valid = false, locked = false;
    char actor[48]{};
    uint8_t digest[32]{};
    uint32_t started = 0U, writes = 0U, renames = 0U, removes = 0U, bytes = 0U;
};

RuntimePolicy active{};
alignas(16) uint8_t rollback_policy[max_policy_bytes]{};
MutationWindow mutation_window{};
uint32_t persistent_policy_version = 0U;
uint32_t protected_records = 0U, writer_records = 0U, audited = 0U, blocked = 0U, allowed = 0U;
bool ready = false;

bool zero_bytes(const uint8_t* data, uint32_t size) {
    uint8_t combined = 0U; for (uint32_t i = 0U; i < size; ++i) combined = static_cast<uint8_t>(combined | data[i]); return combined == 0U;
}
bool canonical_path(const Record& record) {
    if (!record.path_length || record.path_length >= sizeof(record.path) || record.path[0] != '/' || record.path[record.path_length] != 0) return false;
    for (uint32_t i = record.path_length + 1U; i < sizeof(record.path); ++i) if (record.path[i] != 0) return false;
    return true;
}
bool path_matches(const char* path, const char* prefix) {
    if (!path || !prefix || !string_starts_with(path, prefix)) return false;
    const uint32_t length = static_cast<uint32_t>(string_length(prefix));
    return path[length] == 0 || path[length] == '/';
}
bool record_valid(const Record& record) {
    if ((record.type != record_protected && record.type != record_writer) || !record.operations ||
        (record.operations & ~known_operations) != 0U || !canonical_path(record) || !zero_bytes(record.reserved, sizeof(record.reserved))) return false;
    if (record.type == record_protected) return zero_bytes(record.digest, sizeof(record.digest));
    return !zero_bytes(record.digest, sizeof(record.digest));
}

Validation validate(const uint8_t* data, uint32_t size, uint32_t minimum_version, RuntimePolicy& output) {
    memset(&output, 0, sizeof(output));
    if (!data || size < sizeof(Header) + signature_bytes || size > max_policy_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(data);
    static constexpr char magic[4] = {'Z','R','W','P'};
    if (!storage::bytes_equal(header->magic, magic, sizeof(magic)) || header->schema != schema_version ||
        header->header_size != sizeof(Header) || header->mode > mode_block || !header->record_count || header->record_count > max_records ||
        header->payload_size != header->record_count * sizeof(Record) || sizeof(Header) + header->payload_size + signature_bytes != size ||
        !header->window_ticks || !header->max_writes || !header->max_renames || !header->max_removes || !header->max_bytes ||
        !zero_bytes(header->reserved, sizeof(header->reserved))) return Validation::malformed;
    if (header->minimum_engine > engine_version) return Validation::engine;
    if (!storage::bytes_equal(header->key_id, kZrwpRootKeyId, sizeof(header->key_id))) return Validation::key;
    uint8_t digest[32];
    security_guard::sha256(data + sizeof(Header), header->payload_size, digest);
    if (!security_guard::digest_equal(digest, header->payload_sha256)) return Validation::digest;
    security_guard::sha256(data, size - signature_bytes, digest);
    if (!zgdb::rsa_pss_verify_with_modulus(data + size - signature_bytes, digest, kZrwpRootModulus)) return Validation::signature;
    if (header->policy_version < minimum_version) return Validation::rollback;
    const auto* records = reinterpret_cast<const Record*>(data + sizeof(Header));
    uint32_t protected_count = 0U, writer_count = 0U;
    for (uint32_t i = 0U; i < header->record_count; ++i) {
        if (!record_valid(records[i])) return Validation::records;
        for (uint32_t previous = 0U; previous < i; ++previous) {
            if (records[i].type == records[previous].type && string_equal(records[i].path, records[previous].path)) return Validation::records;
        }
        if (records[i].type == record_protected) ++protected_count; else ++writer_count;
        output.records[i] = records[i];
    }
    if (!protected_count || !writer_count) return Validation::records;
    output.version = header->policy_version;
    output.mode = header->mode;
    output.record_count = header->record_count;
    output.window_ticks = header->window_ticks;
    output.max_writes = header->max_writes;
    output.max_renames = header->max_renames;
    output.max_removes = header->max_removes;
    output.max_bytes = header->max_bytes;
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
    char text[12]{}; char reverse[10]{}; uint32_t count = 0U, value = version;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    for (uint32_t i = 0U; i < count; ++i) text[i] = reverse[count - 1U - i];
    text[count++] = '\n';
    return storage::write_file("/security/ransomware-policy.version", reinterpret_cast<const uint8_t*>(text), count, false);
}
void activate(const RuntimePolicy& policy) {
    active = policy;
    protected_records = writer_records = 0U;
    for (uint32_t i = 0U; i < active.record_count; ++i) {
        if (active.records[i].type == record_protected) ++protected_records; else ++writer_records;
    }
    memset(&mutation_window, 0, sizeof(mutation_window));
}
bool read_policy(const char* path, uint32_t minimum, Validation& validation, RuntimePolicy& output) {
    uint32_t size = 0U;
    validation = Validation::malformed;
    if (!storage::read_file(path, process::application_buffer, max_policy_bytes, size)) return false;
    validation = validate(process::application_buffer, size, minimum, output);
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
        case Validation::records: return "records";
        default: return "malformed";
    }
}

bool protected_operation(const char* path, uint8_t operation) {
    if (!path) return false;
    for (uint32_t i = 0U; i < active.record_count; ++i) {
        const Record& record = active.records[i];
        if (record.type == record_protected && (record.operations & operation) && path_matches(path, record.path)) return true;
    }
    return false;
}
bool writer_allowed(const char* actor, const uint8_t digest[32], uint8_t operation) {
    for (uint32_t i = 0U; i < active.record_count; ++i) {
        const Record& record = active.records[i];
        if (record.type == record_writer && (record.operations & operation) && string_equal(actor, record.path) &&
            security_guard::digest_equal(digest, record.digest)) return true;
    }
    return false;
}
void reset_window(const char* actor, const uint8_t digest[32]) {
    memset(&mutation_window, 0, sizeof(mutation_window));
    mutation_window.valid = true;
    mutation_window.started = timer_ticks;
    storage::path_copy(mutation_window.actor, actor, sizeof(mutation_window.actor));
    memcpy(mutation_window.digest, digest, sizeof(mutation_window.digest));
}
void ensure_window(const char* actor, const uint8_t digest[32]) {
    const bool same = mutation_window.valid && string_equal(mutation_window.actor, actor) && security_guard::digest_equal(mutation_window.digest, digest);
    const bool expired = mutation_window.valid && static_cast<uint32_t>(timer_ticks - mutation_window.started) >= active.window_ticks;
    if (!same || expired) reset_window(actor, digest);
}
const char* operation_name(uint8_t operation) {
    if (operation == operation_write) return "write";
    if (operation == operation_rename) return "rename";
    return "remove";
}
bool audit_decision(const char* marker, const char* target, const char* actor, const uint8_t digest[32], const char* reason,
                    uint8_t operation, bool deny) {
    security_guard::ScanResult event{};
    event.verdict = deny ? security_guard::Verdict::untrusted : security_guard::Verdict::suspicious;
    event.size = operation;
    storage::path_copy(event.path, target, sizeof(event.path));
    storage::path_copy(event.signature, reason, sizeof(event.signature));
    memcpy(event.digest, digest, sizeof(event.digest));
    if (!security_guard::record(security_guard::AuditAction::ransomware, event)) return false;
    serial::write(marker); serial::write(" actor="); serial::write(actor); serial::write(" target="); serial::write(target);
    serial::write(" operation="); serial::write(operation_name(operation)); serial::write(" reason="); serial::write(reason);
    serial::write(" mode="); serial::line(active.mode == mode_block ? "block" : "audit");
    return true;
}

bool authorize(const char* target, uint8_t operation, uint32_t bytes) {
    if (!ready || !target || !(operation & known_operations)) return false;
    char normalized[48]{};
    if (!storage::normalize_path(target, normalized)) return false;
    if (!protected_operation(normalized, operation)) return true;
    char actor[48]{}; uint8_t digest[32]{};
    if (!process::active_security_actor(actor, digest)) return true; // kernel shell and internal trusted services
    ensure_window(actor, digest);
    const bool trusted = writer_allowed(actor, digest, operation);
    const bool write_overflow = operation == operation_write && mutation_window.writes == 0xFFFFFFFFU;
    const bool rename_overflow = operation == operation_rename && mutation_window.renames == 0xFFFFFFFFU;
    const bool remove_overflow = operation == operation_remove && mutation_window.removes == 0xFFFFFFFFU;
    const bool byte_overflow = bytes > 0xFFFFFFFFU - mutation_window.bytes;
    const uint32_t writes = write_overflow ? 0xFFFFFFFFU : mutation_window.writes + (operation == operation_write ? 1U : 0U);
    const uint32_t renames = rename_overflow ? 0xFFFFFFFFU : mutation_window.renames + (operation == operation_rename ? 1U : 0U);
    const uint32_t removes = remove_overflow ? 0xFFFFFFFFU : mutation_window.removes + (operation == operation_remove ? 1U : 0U);
    const uint32_t total_bytes = byte_overflow ? 0xFFFFFFFFU : mutation_window.bytes + bytes;
    const char* reason = nullptr;
    if (!trusted) reason = "untrusted-writer";
    else if (mutation_window.locked) reason = "actor-locked";
    else if (write_overflow || rename_overflow || remove_overflow || byte_overflow) reason = "counter-overflow";
    else if (writes > active.max_writes) reason = "write-budget";
    else if (renames > active.max_renames) reason = "rename-budget";
    else if (removes > active.max_removes) reason = "remove-budget";
    else if (total_bytes > active.max_bytes) reason = "byte-budget";
    if (reason) {
        const bool deny = active.mode == mode_block;
        if (!audit_decision(deny ? "ZRWP_BLOCKED" : "ZRWP_AUDIT", normalized, actor, digest, reason, operation, deny)) return false;
        if (deny) { mutation_window.locked = true; ++blocked; return false; }
        ++audited;
    }
    mutation_window.writes = writes; mutation_window.renames = renames; mutation_window.removes = removes; mutation_window.bytes = total_bytes;
    ++allowed;
    return true;
}
bool authorize_write(const char* path, uint32_t bytes) { return authorize(path, operation_write, bytes); }
bool authorize_copy(const char* destination, uint32_t bytes) { return authorize(destination, operation_write, bytes); }
bool authorize_rename(const char* source, const char* destination) {
    char normalized_source[48]{}, normalized_destination[48]{};
    if (!source || !destination || !storage::normalize_path(source, normalized_source) || !storage::normalize_path(destination, normalized_destination)) return false;
    if (protected_operation(normalized_source, operation_rename) && !authorize(normalized_source, operation_rename, 0U)) return false;
    return !protected_operation(normalized_destination, operation_rename) || authorize(normalized_destination, operation_rename, 0U);
}
bool authorize_remove(const char* path) { return authorize(path, operation_remove, 0U); }

bool restore_update(const RuntimePolicy& previous, uint32_t previous_version, uint32_t previous_size) {
    const bool policy_restored = previous_size && storage::write_file("/security/ransomware-policy.zrwp", rollback_policy, previous_size, false);
    const bool version_restored = policy_restored && write_version(previous_version);
    const bool metadata_synced = version_restored && storage::sync_metadata();
    if (!policy_restored || !version_restored || !metadata_synced) {
        ready = false;
        serial::line("ZRWP_UPDATE_ROLLBACK_FAILED");
        return false;
    }
    activate(previous);
    persistent_policy_version = previous_version;
    ready = true;
    serial::line("ZRWP_UPDATE_ROLLBACK_OK");
    return true;
}

bool update(const char* candidate_path) {
    Validation validation{};
    RuntimePolicy proposed{};
    if (!candidate_path || !*candidate_path || !read_policy(candidate_path, active.version + 1U, validation, proposed)) {
        if (validation == Validation::rollback) serial::write("ZRWP_ROLLBACK_REJECTED reason=");
        else if (validation == Validation::key) serial::write("ZRWP_KEY_REJECTED reason=");
        else serial::write("ZRWP_TAMPER_REJECTED reason=");
        serial::line(validation_name(validation)); return false;
    }
    if (proposed.version != active.version + 1U) { serial::line("ZRWP_SEQUENCE_REJECTED reason=non-sequential"); return false; }

    const RuntimePolicy previous = active;
    const uint32_t previous_version = persistent_policy_version;
    uint32_t previous_size = 0U;
    if (!storage::read_file("/security/ransomware-policy.zrwp", rollback_policy, sizeof(rollback_policy), previous_size) || !previous_size) {
        serial::line("ZRWP_UPDATE_BACKUP_FAILED"); return false;
    }

    uint32_t candidate_size = 0U;
    if (!storage::read_file(candidate_path, process::application_buffer, max_policy_bytes, candidate_size)) return false;
    RuntimePolicy final_policy{};
    validation = validate(process::application_buffer, candidate_size, active.version + 1U, final_policy);
    if (validation != Validation::okay || final_policy.version != proposed.version) return false;
    if (!storage::write_file("/security/ransomware-policy.zrwp", process::application_buffer, candidate_size, false)) return false;

    uint32_t committed_size = 0U;
    if (!storage::read_file("/security/ransomware-policy.zrwp", process::application_buffer, max_policy_bytes, committed_size) ||
        committed_size != candidate_size || validate(process::application_buffer, committed_size, active.version + 1U, final_policy) != Validation::okay ||
        final_policy.version != proposed.version) {
        (void)restore_update(previous, previous_version, previous_size);
        serial::line("ZRWP_UPDATE_READBACK_FAILED"); return false;
    }
    if (!write_version(final_policy.version)) {
        (void)restore_update(previous, previous_version, previous_size);
        serial::line("ZRWP_UPDATE_VERSION_COMMIT_FAILED"); return false;
    }

    activate(final_policy); persistent_policy_version = active.version; ready = true;
    security_guard::ScanResult event{};
    event.verdict = security_guard::Verdict::trusted;
    storage::path_copy(event.path, "/security/ransomware-policy.zrwp", sizeof(event.path));
    storage::path_copy(event.signature, "Ransomware.Policy.Update", sizeof(event.signature));
    security_guard::sha256(process::application_buffer, committed_size, event.digest); event.size = committed_size;
    if (!security_guard::record(security_guard::AuditAction::ransomware_policy, event)) {
        (void)restore_update(previous, previous_version, previous_size);
        serial::line("ZRWP_UPDATE_AUDIT_FAILED"); return false;
    }
    if (!storage::sync_metadata()) {
        (void)restore_update(previous, previous_version, previous_size);
        serial::line("ZRWP_UPDATE_SYNC_FAILED"); return false;
    }
    serial::write("ZRWP_ATOMIC_UPDATE_OK version="); process::serial_u32(active.version); serial::write("\r\n");
    return true;
}

bool self_test() {
    if (!ready || protected_records != 2U || writer_records != 1U || active.max_writes != 1U || active.max_bytes != 128U) return false;
    if (!protected_operation("/apps/userio.txt", operation_write) || !protected_operation("/docs/readme.txt", operation_remove) ||
        protected_operation("/apps/other.txt", operation_write)) return false;
    static constexpr uint8_t fileio_digest[32] = {0x5a,0xcc,0x70,0xa7,0x8b,0xd8,0x30,0xcd,0x7b,0x04,0x79,0x9b,0xf3,0xc2,0xbc,0x22,
        0x90,0x5a,0xc5,0x30,0x70,0xdb,0x00,0xb5,0x35,0x68,0x7c,0x8b,0x70,0x3a,0x93,0x4e};
    return writer_allowed("/apps/fileio.elf", fileio_digest, operation_write) && !writer_allowed("/apps/args.elf", fileio_digest, operation_write);
}

bool init() {
    uint8_t version_buffer[16]{}; uint32_t version_size = 0U;
    persistent_policy_version = storage::read_file("/security/ransomware-policy.version", version_buffer, sizeof(version_buffer), version_size)
        ? parse_version(version_buffer, version_size) : 0U;
    uint32_t minimum = persistent_policy_version > compiled_policy_floor ? persistent_policy_version : compiled_policy_floor;
    Validation validation{}; RuntimePolicy loaded{};
    if (!read_policy("/security/ransomware-policy.zrwp", minimum, validation, loaded)) {
        serial::write("ZRWP_INIT_FAILED reason="); serial::line(validation_name(validation)); ready = false; return false;
    }
    activate(loaded);
    if (persistent_policy_version < active.version && !write_version(active.version)) { ready = false; return false; }
    persistent_policy_version = active.version; ready = true;
    if (!self_test()) { ready = false; serial::line("ZRWP_SELFTEST_FAILED"); return false; }
    serial::line("ZRWP_ROOT_KEY_OK id=7186b2bd819e47dc");
    serial::line("ZRWP_PSS_SIGNATURE_OK");
    serial::write("ZRWP_POLICY_VERSION_OK version="); process::serial_u32(active.version); serial::write("\r\n");
    serial::write("ZRWP_POLICY_OK mode="); serial::write(active.mode == mode_block ? "block" : "audit");
    serial::write(" protected="); process::serial_u32(protected_records); serial::write(" writers="); process::serial_u32(writer_records); serial::write("\r\n");
    serial::line("ZRWP_READY");
    return true;
}

void print_status() {
    console::write("  Ransomware CFA "); console::line(ready ? (active.mode == mode_block ? "block" : "audit") : "unavailable / fail-closed");
    console::write("  Policy         "); console::unsigned_dec(active.version); console::put('\n');
    console::write("  Protected      "); console::unsigned_dec(protected_records); console::put('\n');
    console::write("  Writers        "); console::unsigned_dec(writer_records); console::put('\n');
    console::write("  Window ticks   "); console::unsigned_dec(active.window_ticks); console::put('\n');
    console::write("  Write budget   "); console::unsigned_dec(active.max_writes); console::put('\n');
    console::write("  Byte budget    "); console::unsigned_dec(active.max_bytes); console::put('\n');
    console::write("  Audited        "); console::unsigned_dec(audited); console::put('\n');
    console::write("  Blocked        "); console::unsigned_dec(blocked); console::put('\n');
    console::line("  Root ID        7186b2bd819e47dc");
    serial::line("ZRWP_STATUS_OK");
}
} // namespace zrwp
