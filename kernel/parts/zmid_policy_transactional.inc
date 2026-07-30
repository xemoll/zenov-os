#define update update_unhardened [[maybe_unused]]
#include "zmid_policy.inc"
#undef update

namespace zmid {
bool transaction_write_version(uint32_t value) {
    char text[12]{};
    uint32_t count = 0U;
    if (!security_policy_format::format_nonzero_decimal_u32(value, text, sizeof(text), count)) return false;
    return storage::write_file("/security/zenovguard-intelligence.version",
        reinterpret_cast<const uint8_t*>(text), count, false);
}

bool transaction_restore_update(uint32_t previous_version, uint32_t previous_size) {
    const bool database_restored = security_policy_transaction::restore_verified(
        "/security/zenovguard-intelligence.zmid", previous_size,
        process::application_buffer, max_database_bytes);
    const bool version_restored = database_restored && transaction_write_version(previous_version) &&
        security_policy_transaction::version_matches(
            "/security/zenovguard-intelligence.version", previous_version);
    const bool metadata_synced = version_restored && storage::sync_metadata();
    security_policy_transaction::release();
    if (!database_restored || !version_restored || !metadata_synced) {
        ready = false;
        serial::line("ZMID_UPDATE_ROLLBACK_FAILED");
        return false;
    }
    persistent_database_version = previous_version;
    ready = true;
    serial::line("ZMID_UPDATE_ROLLBACK_OK");
    return true;
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
        serial::line("ZMID_UPDATE_BACKUP_FAILED");
        return false;
    }

    uint32_t size = 0U;
    if (!storage::read_file(candidate_path, process::application_buffer, max_database_bytes, size)) {
        security_policy_transaction::release();
        serial::line("ZMID_UPDATE_CANDIDATE_READ_FAILED");
        return false;
    }
    uint32_t final_version = 0U, final_count = 0U;
    validation = validate(process::application_buffer, size, next_version,
        candidate_records, final_count, final_version);
    if (validation != Validation::okay || final_version != version || final_count != count) {
        security_policy_transaction::release();
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

    security_policy_transaction::release();
    activate(candidate_records, count, version);
    persistent_database_version = version;
    ready = true;
    serial::write("ZMID_ATOMIC_UPDATE_OK version=");
    process::serial_u32(version);
    serial::write("\r\n");
    return true;
}
} // namespace zmid
