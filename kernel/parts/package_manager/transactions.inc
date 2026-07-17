bool install(const char* path) {
    if (!initialized) { console::warning("Package manager is unavailable."); return false; }
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(path, package_buffer, storage::file_capacity(), package, package_size);
    if (error != native_package::Error::none) {
        console::write("Package verification failed: "); console::line(native_package::error_text(error));
        serial::line("ZENPKG_INSTALL_REJECTED"); return false;
    }
    if (!package_catalog::authorizes_package(package, package_size)) {
        console::warning("Package is not authorized by the active signed catalog.");
        serial::line("ZENPKG_CATALOG_REJECTED"); return false;
    }
    const char* requirement = nullptr;
    if (!manifest_requirements_satisfied(package.manifest, requirement)) {
        console::write("Unsatisfied dependency or conflict: "); console::line(requirement ? requirement : "unknown");
        serial::line("ZENPKG_REQUIREMENT_REJECTED"); return false;
    }
    int32_t index = find_record(package.manifest.name);
    if (index >= 0) {
        const PackageRecord& existing = database.records[index];
        if (string_equal(existing.active.version, package.manifest.version)) {
            if (!security_guard::digest_equal(existing.active.package_digest, package.package_digest)) {
                console::warning("Installed version has a different package digest.");
                serial::line("ZENPKG_SAME_VERSION_DIGEST_CONFLICT"); return false;
            }
            console::success("PKG_ALREADY_INSTALLED"); serial::line("ZENPKG_INSTALL_IDEMPOTENT_OK"); return true;
        }
    } else {
        index = free_record();
        if (index < 0) { console::warning("Package database is full."); return false; }
    }
    const security_guard::ScanResult payload_scan = security_guard::scan_memory(package.manifest.entrypoint, package.payload, package.payload_size, false);
    if (payload_scan.verdict != security_guard::Verdict::clean) {
        console::warning("Package payload failed ZenovGuard appraisal."); serial::line("ZENPKG_SECURITY_APPRAISAL_REJECTED"); return false;
    }
    if (!payload_committed(package)) {
        console::warning("Package payload could not be committed and re-read."); serial::line("ZENPKG_PAYLOAD_COMMIT_FAILED"); return false;
    }
    Database next = database;
    PackageRecord& record = next.records[index];
    if (record.used) record.previous = record.active;
    else {
        memset(&record, 0, sizeof(record)); record.used = 1U;
        if (!copy_text_exact(record.name, sizeof(record.name), package.manifest.name)) return false;
    }
    set_reference(record.active, package, package_size);
    if (!persist_database(next)) {
        console::warning("Package database commit failed; payload remains an inactive orphan.");
        serial::line("ZENPKG_DB_COMMIT_FAILED_SAFE_ORPHAN"); return false;
    }
    console::write("Installed "); console::write(record.name); console::put(' '); console::line(record.active.version);
    console::success("PKG_INSTALL_OK"); serial::line(record.previous.valid ? "ZENPKG_UPGRADE_COMMIT_OK" : "ZENPKG_INSTALL_COMMIT_OK");
    return true;
}

bool reverse_dependency_exists(const char* name, const char* version, const char*& dependent) {
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (!database.records[i].used || string_equal(database.records[i].name, name)) continue;
        for (uint32_t d = 0; d < database.records[i].active.dependency_count; ++d) {
            char dependency_name[32]{}, dependency_version[16]{};
            if (parse_dependency(database.records[i].active.dependencies[d], dependency_name, dependency_version) &&
                string_equal(dependency_name, name) && string_equal(dependency_version, version)) {
                dependent = database.records[i].name; return true;
            }
        }
    }
    return false;
}

bool remove_package(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) { console::warning("Package is not installed."); serial::line("ZENPKG_NOT_INSTALLED"); return false; }
    const char* dependent = nullptr;
    if (reverse_dependency_exists(name, database.records[index].active.version, dependent)) {
        console::write("Package is required by "); console::line(dependent); serial::line("ZENPKG_REMOVE_DEPENDENCY_BLOCKED"); return false;
    }
    const PackageRecord removed = database.records[index];
    Database next = database;
    memset(&next.records[index], 0, sizeof(next.records[index]));
    if (!persist_database(next)) { console::warning("Package database removal commit failed."); return false; }
    if (removed.active.valid) (void)storage::remove(removed.active.entrypoint);
    if (removed.previous.valid && !same_storage_path(removed.previous.entrypoint, removed.active.entrypoint)) (void)storage::remove(removed.previous.entrypoint);
    (void)storage::sync_metadata();
    console::success("PKG_REMOVE_OK"); serial::line("ZENPKG_REMOVE_COMMIT_OK"); return true;
}

bool rollback(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0 || !database.records[index].previous.valid) {
        console::warning("No previous package version is available."); serial::line("ZENPKG_ROLLBACK_UNAVAILABLE"); return false;
    }
    Database next = database;
    PackageReference temporary = next.records[index].active;
    next.records[index].active = next.records[index].previous;
    next.records[index].previous = temporary;
    const char* reason = nullptr;
    if (!reference_requirements_satisfied(next, name, next.records[index].active, reason)) {
        console::write("Rollback requirements are not satisfied: "); console::line(reason ? reason : "unknown"); return false;
    }
    storage::FileInfo info{};
    if (!storage::query(next.records[index].active.entrypoint, info) || info.type != storage::zfs_type_file || info.size != next.records[index].active.payload_size) {
        console::warning("Previous package payload is missing."); return false;
    }
    if (!persist_database(next)) { console::warning("Rollback database commit failed."); return false; }
    console::write("Rolled back "); console::write(name); console::write(" to "); console::line(database.records[index].active.version);
    console::success("PKG_ROLLBACK_OK"); serial::line("ZENPKG_ROLLBACK_COMMIT_OK"); return true;
}

bool candidate_preserves_installed() {
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (!record.used) continue;
        const PackageReference* references[2] = {&record.active, &record.previous};
        for (const PackageReference* reference : references) {
            if (!reference->valid) continue;
            if (!package_catalog::candidate_authorizes(record.name, reference->version, reference->entrypoint, reference->payload_type,
                    reference->package_size, reference->payload_size, reference->package_digest, reference->payload_digest)) {
                console::write("Catalog would orphan installed package: "); console::write(record.name); console::put(' '); console::line(reference->version);
                serial::line("ZENPKG_CATALOG_INSTALLED_SET_REJECTED"); return false;
            }
        }
    }
    return true;
}

bool update_catalog(const char* path) {
    if (!initialized || !package_catalog::prepare_candidate(path) || !candidate_preserves_installed()) return false;
    if (!package_catalog::commit_candidate(path)) { serial::line("ZENPKG_CATALOG_COMMIT_FAILED"); return false; }
    if (!database_valid(database, true)) { initialized = false; serial::line("ZENPKG_DB_CATALOG_REVALIDATION_FAILED"); return false; }
    return true;
}
