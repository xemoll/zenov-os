bool install(const char* path) {
    if (!initialized) { console::warning("Package manager is unavailable."); return false; }
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(path, package_buffer, storage::file_capacity(), package, package_size);
    if (error != native_package::Error::none) {
        console::write("Package verification failed: "); console::line(native_package::error_text(error));
        serial::line("ZENPKG_INSTALL_REJECTED");
        return false;
    }
    if (!authorized_package(package)) {
        console::warning("Package is structurally valid but is not authorized by the 0.1.1 bootstrap catalog.");
        serial::line("ZENPKG_UNAUTHORIZED_PACKAGE");
        return false;
    }
    const char* requirement = nullptr;
    if (!requirements_satisfied(package.manifest, requirement)) {
        console::write("Unsatisfied dependency or conflict: "); console::line(requirement ? requirement : "unknown");
        serial::line("ZENPKG_REQUIREMENT_REJECTED");
        return false;
    }

    int32_t index = find_record(package.manifest.name);
    if (index >= 0) {
        const PackageRecord& existing = database.records[index];
        const int order = version_compare(package.manifest.version, existing.active.version);
        if (order == 0) {
            if (!digest_equal(existing.active.package_digest, package.package_digest)) {
                console::warning("Installed version has a different package digest.");
                serial::line("ZENPKG_SAME_VERSION_DIGEST_CONFLICT");
                return false;
            }
            console::success("PKG_ALREADY_INSTALLED");
            serial::line("ZENPKG_INSTALL_IDEMPOTENT_OK");
            return true;
        }
        if (order < 0) {
            console::warning("Direct package downgrade is blocked; use pkg rollback for the retained previous version.");
            serial::line("ZENPKG_DOWNGRADE_REJECTED");
            return false;
        }
    } else {
        index = free_record();
        if (index < 0) { console::warning("Package database is full."); return false; }
    }

    const security_guard::ScanResult payload_scan = security_guard::scan_memory(
        package.manifest.entrypoint, package.payload, package.payload_size, false);
    if (payload_scan.verdict != security_guard::Verdict::clean) {
        console::warning("Package payload failed ZenovGuard appraisal.");
        serial::line("ZENPKG_SECURITY_APPRAISAL_REJECTED");
        return false;
    }

    if (!payload_committed(package)) {
        console::warning("Package payload could not be committed.");
        serial::line("ZENPKG_PAYLOAD_COMMIT_FAILED");
        return false;
    }

    Database next = database;
    PackageRecord& record = next.records[index];
    PackageReference obsolete{};
    if (record.used) { obsolete = record.previous; record.previous = record.active; }
    else {
        memset(&record, 0, sizeof(record));
        record.used = 1U;
        if (!copy_text_exact(record.name, sizeof(record.name), package.manifest.name)) return false;
    }
    set_reference(record.active, package);
    if (!persist_database(next)) {
        console::warning("Package database commit failed; payload remains inactive and safe to collect.");
        serial::line("ZENPKG_DB_COMMIT_FAILED_SAFE_ORPHAN");
        return false;
    }
    if (obsolete.valid && !string_equal(obsolete.entrypoint, record.active.entrypoint) &&
        !string_equal(obsolete.entrypoint, record.previous.entrypoint)) {
        (void)storage::package_remove(obsolete.entrypoint);
        (void)storage::sync_metadata();
    }

    console::write("Installed "); console::write(record.name); console::put(' '); console::line(record.active.version);
    console::success("PKG_INSTALL_OK");
    serial::line(record.previous.valid ? "ZENPKG_UPGRADE_COMMIT_OK" : "ZENPKG_INSTALL_COMMIT_OK");
    return true;
}

bool remove_package(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) { console::warning("Package is not installed."); serial::line("ZENPKG_NOT_INSTALLED"); return false; }
    const PackageRecord removed = database.records[index];
    Database next = database;
    memset(&next.records[index], 0, sizeof(next.records[index]));
    if (!persist_database(next)) { console::warning("Package database removal commit failed."); return false; }
    if (removed.active.valid) storage::package_remove(removed.active.entrypoint);
    if (removed.previous.valid && !string_equal(removed.previous.entrypoint, removed.active.entrypoint)) storage::package_remove(removed.previous.entrypoint);
    storage::sync_metadata();
    console::success("PKG_REMOVE_OK");
    serial::line("ZENPKG_REMOVE_COMMIT_OK");
    return true;
}

bool rollback(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0 || !database.records[index].previous.valid) {
        console::warning("No previous package version is available.");
        serial::line("ZENPKG_ROLLBACK_UNAVAILABLE");
        return false;
    }
    Database next = database;
    PackageReference temporary = next.records[index].active;
    next.records[index].active = next.records[index].previous;
    next.records[index].previous = temporary;
    if (!reference_payload_valid(name, next.records[index].active)) {
        console::warning("Previous package payload is missing or does not match its recorded SHA-256.");
        serial::line("ZENPKG_ROLLBACK_PAYLOAD_INVALID");
        return false;
    }
    if (!persist_database(next)) { console::warning("Rollback database commit failed."); return false; }
    console::write("Rolled back "); console::write(name); console::write(" to "); console::line(database.records[index].active.version);
    console::success("PKG_ROLLBACK_OK");
    serial::line("ZENPKG_ROLLBACK_COMMIT_OK");
    return true;
}
