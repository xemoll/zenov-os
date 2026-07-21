constexpr const char* transport_state_path = "/var/lib/zenpkg/transport.v1";
constexpr uint32_t transport_chunk_bytes = 512U;
constexpr uint32_t transport_retry_limit = 3U;
constexpr uint32_t transport_provider_offline = 1U;
constexpr uint32_t transport_phase_downloading = 1U;
constexpr uint32_t transport_phase_ready = 2U;
constexpr uint32_t transport_phase_failed = 3U;

struct [[gnu::packed]] TransportJournal {
    char magic[8];
    uint32_t schema;
    uint32_t generation;
    uint32_t phase;
    uint32_t offset;
    uint32_t expected_length;
    uint32_t provider;
    uint32_t retries;
    uint32_t checksum;
    char name[32];
    char version[16];
    char source_path[48];
    char partial_path[48];
    uint8_t package_digest[32];
    uint8_t prefix_digest[32];
};

static_assert(sizeof(TransportJournal) == 248U);

enum class TransportOutcome : uint8_t { failed, pending, complete };

TransportJournal transport_journal{};
bool transport_journal_present = false;

const char* transport_phase_name(uint32_t phase) {
    if (phase == transport_phase_downloading) return "downloading";
    if (phase == transport_phase_ready) return "ready";
    if (phase == transport_phase_failed) return "failed";
    return "invalid";
}

void transport_report(const char* marker, const package_repository::Target& target,
    uint32_t offset, uint32_t attempt) {
    serial::write(marker);
    serial::write(" name=");
    serial::write(target.name);
    serial::write(" version=");
    serial::write(target.version);
    serial::write(" offset=");
    process::serial_u32(offset);
    serial::write(" expected=");
    process::serial_u32(target.package_length);
    serial::write(" attempt=");
    process::serial_u32(attempt);
    serial::write("\r\n");
}

uint32_t transport_journal_checksum(const TransportJournal& input) {
    TransportJournal copy = input;
    copy.checksum = 0;
    return storage::fnv1a(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool transport_magic_valid(const TransportJournal& input) {
    const char expected[8] = {'Z','P','T','R','N','1',0,0};
    return storage::bytes_equal(input.magic, expected, sizeof(expected));
}

bool transport_journal_valid(const TransportJournal& input) {
    if (!transport_magic_valid(input) || input.schema != 1U || input.generation == 0U ||
        input.provider != transport_provider_offline || input.expected_length == 0U ||
        input.expected_length > maximum_package_bytes || input.offset > input.expected_length ||
        input.checksum != transport_journal_checksum(input)) return false;
    if (input.phase != transport_phase_downloading && input.phase != transport_phase_ready &&
        input.phase != transport_phase_failed) return false;
    if (input.phase == transport_phase_ready && input.offset != input.expected_length) return false;
    if (!input.name[0] || !input.version[0] || !input.source_path[0] || !input.partial_path[0] ||
        input.name[sizeof(input.name) - 1U] || input.version[sizeof(input.version) - 1U] ||
        input.source_path[sizeof(input.source_path) - 1U] || input.partial_path[sizeof(input.partial_path) - 1U]) return false;
    return true;
}

bool transport_read_journal(TransportJournal& output) {
    uint32_t size = 0;
    if (!storage::read_file(transport_state_path, reinterpret_cast<uint8_t*>(&output), sizeof(output), size)) return false;
    return size == sizeof(output) && transport_journal_valid(output);
}

bool transport_persist_journal(TransportJournal next) {
    if (transport_journal_present) {
        if (transport_journal.generation == 0xFFFFFFFFU) return false;
        next.generation = transport_journal.generation + 1U;
    } else if (next.generation == 0U) {
        next.generation = 1U;
    }
    next.checksum = transport_journal_checksum(next);
    if (!transport_journal_valid(next) ||
        !storage::package_write_file(transport_state_path, reinterpret_cast<const uint8_t*>(&next), sizeof(next), false) ||
        !storage::sync_metadata()) return false;
    TransportJournal recovered{};
    if (!transport_read_journal(recovered) || recovered.generation != next.generation ||
        recovered.checksum != next.checksum) return false;
    transport_journal = recovered;
    transport_journal_present = true;
    return true;
}

bool transport_remove_journal() {
    storage::FileInfo info{};
    if (storage::query(transport_state_path, info)) {
        if (info.type != storage::zfs_type_file || !storage::package_transport_remove(transport_state_path) ||
            !storage::sync_metadata()) return false;
    }
    memset(&transport_journal, 0, sizeof(transport_journal));
    transport_journal_present = false;
    return true;
}

bool transport_load_verified_source(const char* source_path, const package_repository::Target& target,
    uint32_t& source_size) {
    native_package::VerifiedPackage package{};
    source_size = 0;
    const native_package::Error error = native_package::verify_file(
        source_path, package_buffer, maximum_package_bytes, package, source_size);
    return error == native_package::Error::none && package_matches_target(package, source_size, target);
}

bool transport_partial_prefix_valid(const char* source_path, const char* partial_path,
    const package_repository::Target& target, uint32_t partial_size, uint32_t& source_size,
    uint8_t prefix_digest[32]) {
    if (!transport_load_verified_source(source_path, target, source_size) || partial_size > source_size) return false;
    crypto::sha256(package_buffer, partial_size, prefix_digest);
    if (partial_size == 0U) return true;
    uint8_t source_prefix_digest[32]{};
    memcpy(source_prefix_digest, prefix_digest, sizeof(source_prefix_digest));
    uint32_t loaded_size = 0;
    if (!storage::read_file(partial_path, package_buffer, maximum_package_bytes, loaded_size) ||
        loaded_size != partial_size) return false;
    uint8_t stored_prefix_digest[32]{};
    crypto::sha256(package_buffer, loaded_size, stored_prefix_digest);
    const bool matches = crypto::digest_equal(source_prefix_digest, stored_prefix_digest);
    return transport_load_verified_source(source_path, target, source_size) && matches;
}

bool transport_target_matches_journal(const package_repository::Target& target,
    const TransportJournal& journal) {
    return string_equal(target.name, journal.name) && string_equal(target.version, journal.version) &&
        target.package_length == journal.expected_length &&
        crypto::digest_equal(target.package_digest, journal.package_digest);
}

const package_repository::Target* transport_journal_target() {
    if (!transport_journal_present) return nullptr;
    for (uint32_t i = 0; i < package_repository::target_count(); ++i) {
        const package_repository::Target* target = package_repository::target_at(i);
        if (target && transport_target_matches_journal(*target, transport_journal)) return target;
    }
    return nullptr;
}

bool transport_remove_partial_for_target(const package_repository::Target& target) {
    char partial_path[48]{};
    if (!cache_path(target, true, partial_path)) return false;
    storage::FileInfo info{};
    if (!storage::query(partial_path, info)) return true;
    return storage::package_cache_remove(partial_path) && storage::sync_metadata();
}

bool transport_reset_target(const package_repository::Target& target, const char* marker) {
    if (!transport_remove_partial_for_target(target) || !transport_remove_journal()) return false;
    serial::line(marker);
    return true;
}

bool transport_reconcile_active(const package_repository::Target& target) {
    char source_path[48]{}, partial_path[48]{};
    if (!repository_source_path(target, source_path) || !cache_path(target, true, partial_path) ||
        !string_equal(source_path, transport_journal.source_path) ||
        !string_equal(partial_path, transport_journal.partial_path)) return false;

    storage::FileInfo partial_info{};
    const bool partial_present = storage::query(partial_path, partial_info);
    const uint32_t actual_size = partial_present ? partial_info.size : 0U;
    if (partial_present && partial_info.type != storage::zfs_type_file) return false;
    uint32_t source_size = 0;
    uint8_t prefix_digest[32]{};
    if (!transport_partial_prefix_valid(source_path, partial_path, target, actual_size, source_size, prefix_digest) ||
        source_size != target.package_length) return false;

    if (actual_size == target.package_length && !verified_target_file(partial_path, target)) return false;
    const uint32_t expected_phase = actual_size == target.package_length ?
        transport_phase_ready : transport_phase_downloading;
    if (actual_size != transport_journal.offset || expected_phase != transport_journal.phase ||
        !crypto::digest_equal(prefix_digest, transport_journal.prefix_digest)) {
        TransportJournal reconciled = transport_journal;
        reconciled.offset = actual_size;
        reconciled.phase = expected_phase;
        reconciled.retries = 0;
        memcpy(reconciled.prefix_digest, prefix_digest, sizeof(reconciled.prefix_digest));
        if (!transport_persist_journal(reconciled)) return false;
        transport_report("ZENPKG_TRANSPORT_JOURNAL_RECONCILED", target, actual_size, 0U);
    }
    return true;
}

bool transport_start_target(const package_repository::Target& target) {
    char source_path[48]{}, partial_path[48]{};
    if (!repository_source_path(target, source_path) || !cache_path(target, true, partial_path)) return false;
    storage::FileInfo partial_info{};
    const bool partial_present = storage::query(partial_path, partial_info);
    if (partial_present && partial_info.type != storage::zfs_type_file) {
        if (!transport_remove_partial_for_target(target)) return false;
    }
    const uint32_t partial_size = partial_present && partial_info.type == storage::zfs_type_file ? partial_info.size : 0U;
    uint32_t source_size = 0;
    uint8_t prefix_digest[32]{};
    if (!transport_partial_prefix_valid(source_path, partial_path, target, partial_size, source_size, prefix_digest) ||
        source_size != target.package_length) {
        if (!transport_remove_partial_for_target(target)) return false;
        memset(prefix_digest, 0, sizeof(prefix_digest));
        if (!transport_load_verified_source(source_path, target, source_size) || source_size != target.package_length) return false;
    }

    TransportJournal journal{};
    const char magic[8] = {'Z','P','T','R','N','1',0,0};
    memcpy(journal.magic, magic, sizeof(magic));
    journal.schema = 1U;
    journal.phase = partial_size == target.package_length ? transport_phase_ready : transport_phase_downloading;
    journal.offset = partial_size;
    journal.expected_length = target.package_length;
    journal.provider = transport_provider_offline;
    if (!copy_text_exact(journal.name, sizeof(journal.name), target.name) ||
        !copy_text_exact(journal.version, sizeof(journal.version), target.version) ||
        !copy_text_exact(journal.source_path, sizeof(journal.source_path), source_path) ||
        !copy_text_exact(journal.partial_path, sizeof(journal.partial_path), partial_path)) return false;
    memcpy(journal.package_digest, target.package_digest, sizeof(journal.package_digest));
    memcpy(journal.prefix_digest, prefix_digest, sizeof(journal.prefix_digest));
    if (!transport_persist_journal(journal)) return false;
    transport_report(partial_size ? "ZENPKG_TRANSPORT_ADOPTED_PARTIAL" : "ZENPKG_TRANSPORT_BEGIN",
        target, partial_size, 0U);
    return true;
}

bool transport_prepare_target(const package_repository::Target& target) {
    if (!transport_journal_present) return transport_start_target(target);
    if (!transport_target_matches_journal(target, transport_journal)) {
        serial::write("ZENPKG_TRANSPORT_BUSY active=");
        serial::write(transport_journal.name);
        serial::write(" requested=");
        serial::line(target.name);
        return false;
    }
    if (!transport_reconcile_active(target)) return transport_reset_target(target, "ZENPKG_TRANSPORT_PARTIAL_RESET") &&
        transport_start_target(target);
    if (transport_journal.phase == transport_phase_failed) {
        TransportJournal resumed = transport_journal;
        resumed.phase = transport_phase_downloading;
        resumed.retries = 0;
        if (!transport_persist_journal(resumed)) return false;
    }
    transport_report(transport_journal.offset ? "ZENPKG_TRANSPORT_RESUME" : "ZENPKG_TRANSPORT_BEGIN",
        target, transport_journal.offset, 0U);
    return true;
}

TransportOutcome transport_transfer(const char* source_path, const char* partial_path,
    const package_repository::Target& target, uint32_t chunk_budget) {
    if (!transport_prepare_target(target)) return TransportOutcome::failed;
    if (!string_equal(source_path, transport_journal.source_path) ||
        !string_equal(partial_path, transport_journal.partial_path)) return TransportOutcome::failed;
    if (transport_journal.phase == transport_phase_ready) return TransportOutcome::complete;

    uint32_t committed_chunks = 0;
    while (transport_journal.offset < target.package_length && committed_chunks < chunk_budget) {
        bool committed = false;
        for (uint32_t attempt = 1U; attempt <= transport_retry_limit; ++attempt) {
            uint32_t source_size = 0;
            if (!transport_load_verified_source(source_path, target, source_size) ||
                source_size != target.package_length || transport_journal.offset > source_size) return TransportOutcome::failed;
            uint32_t chunk = source_size - transport_journal.offset;
            if (chunk > transport_chunk_bytes) chunk = transport_chunk_bytes;
            const uint32_t next_offset = transport_journal.offset + chunk;
            bool write_ok = storage::package_cache_write_partial(
                partial_path, package_buffer + transport_journal.offset, chunk, transport_journal.offset != 0U) &&
                storage::sync_metadata();
            storage::FileInfo committed_info{};
            write_ok = write_ok && storage::query(partial_path, committed_info) &&
                committed_info.type == storage::zfs_type_file && committed_info.size == next_offset &&
                committed_info.size <= target.package_length;
            uint8_t prefix_digest[32]{};
            uint32_t verified_source_size = 0;
            write_ok = write_ok && transport_partial_prefix_valid(source_path, partial_path, target,
                next_offset, verified_source_size, prefix_digest) && verified_source_size == target.package_length;
            if (write_ok) {
                TransportJournal next = transport_journal;
                next.offset = next_offset;
                next.phase = transport_phase_downloading;
                next.retries = 0;
                memcpy(next.prefix_digest, prefix_digest, sizeof(next.prefix_digest));
                if (!transport_persist_journal(next)) return TransportOutcome::failed;
                transport_report("ZENPKG_TRANSPORT_CHUNK_COMMIT", target, next_offset, attempt);
                committed = true;
                ++committed_chunks;
                break;
            }

            TransportJournal failed_attempt = transport_journal;
            failed_attempt.retries = attempt;
            if (!transport_persist_journal(failed_attempt)) return TransportOutcome::failed;
            transport_report("ZENPKG_TRANSPORT_RETRY", target, transport_journal.offset, attempt);
            if (!transport_reconcile_active(target)) break;
        }
        if (!committed) {
            TransportJournal failed = transport_journal;
            failed.phase = transport_phase_failed;
            if (!transport_persist_journal(failed)) return TransportOutcome::failed;
            serial::line("ZENPKG_TRANSPORT_RETRY_EXHAUSTED");
            return TransportOutcome::failed;
        }
    }

    if (transport_journal.offset == target.package_length) {
        if (!verified_target_file(partial_path, target)) {
            TransportJournal failed = transport_journal;
            failed.phase = transport_phase_failed;
            if (!transport_persist_journal(failed)) return TransportOutcome::failed;
            serial::line("ZENPKG_TRANSPORT_FINAL_VERIFY_REJECTED");
            return TransportOutcome::failed;
        }
        TransportJournal ready = transport_journal;
        ready.phase = transport_phase_ready;
        ready.retries = 0;
        if (!transport_persist_journal(ready)) return TransportOutcome::failed;
        transport_report("ZENPKG_TRANSPORT_COMPLETE", target, ready.offset, 0U);
        return TransportOutcome::complete;
    }

    transport_report("ZENPKG_TRANSPORT_PAUSED", target, transport_journal.offset, 0U);
    return TransportOutcome::pending;
}

bool transport_commit_finished(const package_repository::Target& target) {
    if (!transport_journal_present) return true;
    if (!transport_target_matches_journal(target, transport_journal)) return false;
    if (!transport_remove_journal()) return false;
    cache_report_target("ZENPKG_TRANSPORT_JOURNAL_CLEARED", target);
    return true;
}

void transport_status() {
    if (!transport_journal_present) {
        console::line("  State         idle");
        serial::line("ZENPKG_TRANSPORT_STATUS_IDLE");
        return;
    }
    console::write("  State         ");
    console::line(transport_phase_name(transport_journal.phase));
    console::write("  Package       ");
    console::write(transport_journal.name);
    console::put(' ');
    console::line(transport_journal.version);
    console::write("  Progress      ");
    console::unsigned_dec(transport_journal.offset);
    console::write(" / ");
    console::unsigned_dec(transport_journal.expected_length);
    console::line(" bytes");
    console::write("  Generation    ");
    console::unsigned_dec(transport_journal.generation);
    console::put('\n');
    serial::write("ZENPKG_TRANSPORT_STATUS phase=");
    serial::write(transport_phase_name(transport_journal.phase));
    serial::write(" name=");
    serial::write(transport_journal.name);
    serial::write(" version=");
    serial::write(transport_journal.version);
    serial::write(" offset=");
    process::serial_u32(transport_journal.offset);
    serial::write(" expected=");
    process::serial_u32(transport_journal.expected_length);
    serial::write(" generation=");
    process::serial_u32(transport_journal.generation);
    serial::write("\r\n");
}

bool transport_cancel() {
    const package_repository::Target* target = transport_journal_target();
    if (target && !transport_remove_partial_for_target(*target)) return false;
    if (!transport_remove_journal()) return false;
    serial::line("ZENPKG_TRANSPORT_CANCEL_OK");
    return true;
}

bool transport_initialize() {
    storage::FileInfo info{};
    if (!storage::query(transport_state_path, info)) {
        serial::line("ZENPKG_TRANSPORT_IDLE");
        return true;
    }
    TransportJournal loaded{};
    if (info.type != storage::zfs_type_file || !transport_read_journal(loaded)) {
        if (!storage::package_transport_remove(transport_state_path) || !storage::sync_metadata()) return false;
        serial::line("ZENPKG_TRANSPORT_JOURNAL_INVALID_RESET");
        return true;
    }
    transport_journal = loaded;
    transport_journal_present = true;
    const package_repository::Target* target = transport_journal_target();
    if (!target) {
        if (!transport_remove_journal()) return false;
        serial::line("ZENPKG_TRANSPORT_STALE_RESET");
        return true;
    }
    char final_path[48]{};
    if (!cache_path(*target, false, final_path)) return false;
    if (verified_target_file(final_path, *target)) {
        if (!transport_remove_journal()) return false;
        cache_report_target("ZENPKG_TRANSPORT_COMMIT_RECOVERED", *target);
        return true;
    }
    if (!transport_reconcile_active(*target)) {
        if (!transport_reset_target(*target, "ZENPKG_TRANSPORT_RECOVERY_RESET")) return false;
        return true;
    }
    transport_report("ZENPKG_TRANSPORT_JOURNAL_RECOVERED", *target,
        transport_journal.offset, transport_journal.retries);
    return true;
}
