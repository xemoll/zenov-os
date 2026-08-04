#define init init_unhardened [[maybe_unused]]
#define update update_unhardened [[maybe_unused]]
#define classify classify_raw
#define print_status print_status_raw
#include "zmid_policy.inc"
#undef print_status
#undef classify
#undef update
#undef init
#include "zmid_content_workspace.inc"
#define heap zmid_content_workspace_allocator
#include "zmid_content_inspection.inc"
#undef heap

namespace zmid {
bool transaction_write_version(uint32_t value) {
    char text[12]{};
    uint32_t count = 0U;
    if (!security_policy_format::format_nonzero_decimal_u32(value, text, sizeof(text), count)) return false;
    return storage::write_file("/security/zenovguard-intelligence.version",
        reinterpret_cast<const uint8_t*>(text), count, false);
}

bool init() {
    uint8_t version_buffer[16]{};
    uint32_t version_size = 0U;
    if (!storage::read_file("/security/zenovguard-intelligence.version", version_buffer,
            sizeof(version_buffer), version_size) ||
        !parse_version(version_buffer, version_size, persistent_database_version)) {
        serial::line("ZMID_INIT_FAILED reason=version-state");
        ready = false;
        return false;
    }

    uint32_t minimum = compiled_database_floor;
    if (persistent_database_version > minimum) minimum = persistent_database_version;
    Validation validation{};
    uint32_t version = 0U, count = 0U;
    if (!read_database("/security/zenovguard-intelligence.zmid", minimum,
            validation, version, count)) {
        serial::write("ZMID_INIT_FAILED reason=");
        serial::line(validation_name(validation));
        ready = false;
        return false;
    }

    activate(candidate_records, count, version);
    if (persistent_database_version < version &&
        (!transaction_write_version(version) ||
         !security_policy_transaction::version_matches(
             "/security/zenovguard-intelligence.version", version) ||
         !storage::sync_metadata())) {
        ready = false;
        serial::line("ZMID_INIT_FAILED reason=version-commit");
        return false;
    }
    persistent_database_version = version;
    ready = true;
    if (!self_test()) {
        ready = false;
        serial::line("ZMID_SELFTEST_FAILED");
        return false;
    }
    if (!content_inspection::init()) {
        ready = false;
        serial::line("ZMID_CONTENT_INSPECTION_FAILED");
        return false;
    }
    serial::line("ZMID_ROOT_KEY_OK id=6ca6a5275544c533");
    serial::line("ZMID_PSS_SIGNATURE_OK");
    serial::write("ZMID_DATABASE_VERSION_OK version=");
    process::serial_u32(version);
    serial::write("\r\n");
    serial::write("ZMID_RULES_OK total=");
    process::serial_u32(active_record_count);
    serial::write(" hashes=");
    process::serial_u32(hash_records);
    serial::write(" patterns=");
    process::serial_u32(pattern_records);
    serial::write(" audit_only=");
    process::serial_u32(audit_only_records);
    serial::write("\r\n");
    serial::line("ZMID_READY");
    return true;
}

void print_status() {
    print_status_raw();
    console::line("  Content       bounded Base64 / case-fold / ZIP / DEFLATE");
    console::line("  Archive caps  depth 2 / 16 entries / 64 KiB expanded / 128x ratio");
    console::write("  Raw hits      "); console::unsigned_dec(content_inspection::raw_hits); console::put('\n');
    console::write("  Decoded hits  "); console::unsigned_dec(content_inspection::transformed_hits); console::put('\n');
    console::write("  Cache hits    "); console::unsigned_dec(content_inspection::cache_hits); console::put('\n');
    console::write("  Unsupported   "); console::unsigned_dec(content_inspection::unsupported_containers); console::put('\n');
    serial::write("ZMID_CONTENT_STATUS_OK raw="); process::serial_u32(content_inspection::raw_hits);
    serial::write(" transformed="); process::serial_u32(content_inspection::transformed_hits);
    serial::write(" cache="); process::serial_u32(content_inspection::cache_hits);
    serial::write(" unsupported="); process::serial_u32(content_inspection::unsupported_containers);
    serial::write("\r\n");
}

bool transaction_restore_update(uint32_t previous_version, [[maybe_unused]] uint32_t previous_size) {
    const bool restored = security_policy_transaction::rollback();
    if (!restored) {
        ready = false;
        serial::line("ZMID_UPDATE_ROLLBACK_FAILED");
        return false;
    }
    persistent_database_version = previous_version;
    ready = true;
    serial::line("ZMID_UPDATE_ROLLBACK_OK");
    return true;
}

bool transaction_discard_prepared() {
    if (security_policy_transaction::discard()) return true;
    ready = false;
    serial::line("ZMID_UPDATE_JOURNAL_DISCARD_FAILED");
    return false;
}

bool update(const char* candidate_path) {
    if (!ready) { serial::line("ZMID_UPDATE_REJECTED reason=policy-not-ready"); return false; }
    Validation validation{};
    uint32_t next_version = 0U;
    if (!security_policy_format::next_version(active_database_version, next_version)) {
        serial::line("ZMID_SEQUENCE_REJECTED reason=version-exhausted");
        return false;
    }

    uint32_t version = 0U, count = 0U;
    if (!candidate_path || !*candidate_path ||
        !read_database(candidate_path, next_version, validation, version, count)) {
        if (validation == Validation::rollback) serial::write("ZMID_ROLLBACK_REJECTED reason=");
        else if (validation == Validation::key) serial::write("ZMID_KEY_REJECTED reason=");
        else serial::write("ZMID_TAMPER_REJECTED reason=");
        serial::line(validation_name(validation));
        return false;
    }
    if (version != next_version) {
        serial::line("ZMID_SEQUENCE_REJECTED reason=non-sequential");
        return false;
    }

    const uint32_t previous_version = persistent_database_version;
    uint32_t previous_size = 0U;
    if (!security_policy_transaction::acquire(
            "/security/zenovguard-intelligence.zmid", max_database_bytes, previous_size)) {
        if (security_policy_transaction::prepare_uncertain()) {
            ready = false;
            serial::line("ZMID_UPDATE_JOURNAL_PREPARE_UNCERTAIN");
        } else {
            serial::line("ZMID_UPDATE_BACKUP_FAILED");
        }
        return false;
    }

    uint32_t size = 0U;
    if (!storage::read_file(candidate_path, process::application_buffer, max_database_bytes, size)) {
        (void)transaction_discard_prepared();
        serial::line("ZMID_UPDATE_CANDIDATE_READ_FAILED");
        return false;
    }
    uint32_t final_version = 0U, final_count = 0U;
    validation = validate(process::application_buffer, size, next_version,
        candidate_records, final_count, final_version);
    if (validation != Validation::okay || final_version != version || final_count != count) {
        (void)transaction_discard_prepared();
        serial::line("ZMID_UPDATE_CANDIDATE_CHANGED");
        return false;
    }

    if (!storage::write_file("/security/zenovguard-intelligence.zmid",
            process::application_buffer, size, false)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZMID_UPDATE_POLICY_COMMIT_FAILED");
        return false;
    }

    uint32_t committed_size = 0U;
    if (!storage::read_file("/security/zenovguard-intelligence.zmid",
            process::application_buffer, max_database_bytes, committed_size) ||
        committed_size != size ||
        validate(process::application_buffer, committed_size, next_version,
            candidate_records, final_count, final_version) != Validation::okay ||
        final_version != version || final_count != count) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZMID_UPDATE_READBACK_FAILED");
        return false;
    }

    if (!transaction_write_version(version)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZMID_UPDATE_VERSION_COMMIT_FAILED");
        return false;
    }
    if (!security_policy_transaction::version_matches(
            "/security/zenovguard-intelligence.version", version)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZMID_UPDATE_VERSION_READBACK_FAILED");
        return false;
    }
    if (!storage::sync_metadata()) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZMID_UPDATE_SYNC_FAILED");
        return false;
    }

    security_guard::ScanResult event{};
    event.verdict = security_guard::Verdict::trusted;
    event.policy_action = action_audit;
    storage::path_copy(event.path, "/security/zenovguard-intelligence.zmid", sizeof(event.path));
    storage::path_copy(event.signature, "Signed.Intelligence.Update", sizeof(event.signature));
    security_guard::sha256(process::application_buffer, committed_size, event.digest);
    event.size = committed_size;
    if (!security_guard::record(security_guard::AuditAction::intelligence_update, event)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZMID_UPDATE_AUDIT_FAILED");
        return false;
    }
    if (!security_policy_transaction::commit()) {
        ready = false;
        serial::line("ZMID_UPDATE_COMMIT_UNCERTAIN");
        return false;
    }

    activate(candidate_records, count, version);
    content_inspection::clear_cache();
    persistent_database_version = version;
    ready = true;
    serial::write("ZMID_ATOMIC_UPDATE_OK version=");
    process::serial_u32(version);
    serial::write("\r\n");
    return true;
}
} // namespace zmid