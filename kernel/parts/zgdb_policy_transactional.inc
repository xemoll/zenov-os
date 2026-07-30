#define init init_unhardened [[maybe_unused]]
#define update update_unhardened [[maybe_unused]]
#include "zgdb_policy.inc"
#undef update
#undef init

namespace zgdb {
bool transaction_write_version(uint32_t value) {
    char text[12]{};
    uint32_t count = 0U;
    if (!security_policy_format::format_nonzero_decimal_u32(value, text, sizeof(text), count)) return false;
    return storage::write_file("/security/zenovguard.version", reinterpret_cast<const uint8_t*>(text), count, false);
}

bool init() {
    uint8_t version_buffer[16]{};
    uint32_t version_size = 0U;
    if (!storage::read_file("/security/zenovguard.version", version_buffer,
            sizeof(version_buffer), version_size) ||
        !parse_version(version_buffer, version_size, persistent_policy_version)) {
        serial::line("ZGDB_INIT_FAILED reason=version-state");
        ready = false;
        return false;
    }

    uint32_t minimum = compiled_policy_floor;
    if (persistent_policy_version > minimum) minimum = persistent_policy_version;
    Validation validation{};
    uint32_t version = 0U, count = 0U;
    if (!read_database("/security/zenovguard.zgdb", minimum,
            validation, version, count)) {
        serial::write("ZGDB_INIT_FAILED reason=");
        serial::line(validation_name(validation));
        ready = false;
        return false;
    }

    activate(candidate_records, count, version);
    if (persistent_policy_version < version &&
        (!transaction_write_version(version) ||
         !security_policy_transaction::version_matches(
             "/security/zenovguard.version", version) ||
         !storage::sync_metadata())) {
        ready = false;
        serial::line("ZGDB_INIT_FAILED reason=version-commit");
        return false;
    }
    persistent_policy_version = version;
    ready = true;
    serial::line("ZGDB_ROOT_KEY_OK id=6f788074c018f5aa");
    serial::line("ZGDB_PSS_SIGNATURE_OK");
    serial::write("ZGDB_POLICY_VERSION_OK version=");
    process::serial_u32(version);
    serial::write("\r\n");
    serial::line("ZGDB_READY");
    return true;
}

bool transaction_restore_update(uint32_t previous_version, uint32_t previous_size) {
    const bool policy_restored = security_policy_transaction::restore_verified(
        "/security/zenovguard.zgdb", previous_size, process::application_buffer, max_database_bytes);
    const bool version_restored = policy_restored && transaction_write_version(previous_version) &&
        security_policy_transaction::version_matches("/security/zenovguard.version", previous_version);
    const bool metadata_synced = version_restored && storage::sync_metadata();
    security_policy_transaction::release();
    if (!policy_restored || !version_restored || !metadata_synced) {
        ready = false;
        serial::line("ZGDB_UPDATE_ROLLBACK_FAILED");
        return false;
    }
    persistent_policy_version = previous_version;
    ready = true;
    serial::line("ZGDB_UPDATE_ROLLBACK_OK");
    return true;
}

bool update(const char* candidate_path) {
    if (!ready) { serial::line("ZGDB_UPDATE_REJECTED reason=policy-not-ready"); return false; }
    Validation validation{};
    uint32_t next_version = 0U;
    if (!security_policy_format::next_version(active_policy_version, next_version)) {
        serial::line("ZGDB_SEQUENCE_REJECTED reason=version-exhausted");
        return false;
    }

    uint32_t version = 0U, count = 0U;
    if (!candidate_path || !*candidate_path ||
        !read_database(candidate_path, next_version, validation, version, count)) {
        if (validation == Validation::rollback) serial::write("ZGDB_ROLLBACK_REJECTED reason=");
        else if (validation == Validation::key) serial::write("ZGDB_KEY_REJECTED reason=");
        else serial::write("ZGDB_TAMPER_REJECTED reason=");
        serial::line(validation_name(validation));
        return false;
    }
    if (version != next_version) {
        serial::line("ZGDB_SEQUENCE_REJECTED reason=non-sequential");
        return false;
    }

    const uint32_t previous_version = persistent_policy_version;
    uint32_t previous_size = 0U;
    if (!security_policy_transaction::acquire(
            "/security/zenovguard.zgdb", max_database_bytes, previous_size)) {
        serial::line("ZGDB_UPDATE_BACKUP_FAILED");
        return false;
    }

    uint32_t size = 0U;
    if (!storage::read_file(candidate_path, process::application_buffer, max_database_bytes, size)) {
        security_policy_transaction::release();
        serial::line("ZGDB_UPDATE_CANDIDATE_READ_FAILED");
        return false;
    }
    uint32_t final_count = 0U, final_version = 0U;
    validation = validate(process::application_buffer, size, next_version,
        candidate_records, final_count, final_version);
    if (validation != Validation::okay || final_version != version || final_count != count) {
        security_policy_transaction::release();
        serial::line("ZGDB_UPDATE_CANDIDATE_CHANGED");
        return false;
    }

    if (!storage::write_file("/security/zenovguard.zgdb",
            process::application_buffer, size, false)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZGDB_UPDATE_POLICY_COMMIT_FAILED");
        return false;
    }

    uint32_t committed_size = 0U;
    if (!storage::read_file("/security/zenovguard.zgdb",
            process::application_buffer, max_database_bytes, committed_size) ||
        committed_size != size ||
        validate(process::application_buffer, committed_size, next_version,
            candidate_records, final_count, final_version) != Validation::okay ||
        final_version != version || final_count != count) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZGDB_UPDATE_READBACK_FAILED");
        return false;
    }

    if (!transaction_write_version(version)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZGDB_UPDATE_VERSION_COMMIT_FAILED");
        return false;
    }
    if (!security_policy_transaction::version_matches(
            "/security/zenovguard.version", version)) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZGDB_UPDATE_VERSION_READBACK_FAILED");
        return false;
    }
    if (!storage::sync_metadata()) {
        (void)transaction_restore_update(previous_version, previous_size);
        serial::line("ZGDB_UPDATE_SYNC_FAILED");
        return false;
    }

    security_policy_transaction::release();
    activate(candidate_records, count, version);
    persistent_policy_version = version;
    ready = true;
    serial::write("ZGDB_ATOMIC_UPDATE_OK version=");
    process::serial_u32(version);
    serial::write("\r\n");
    return true;
}
} // namespace zgdb
