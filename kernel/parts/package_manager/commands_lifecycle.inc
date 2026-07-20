bool same_storage_path(const char* left, const char* right) {
    char normalized_left[48]{}, normalized_right[48]{};
    return left && right && storage::normalize_path(left, normalized_left) &&
        storage::normalize_path(right, normalized_right) &&
        string_equal(normalized_left, normalized_right);
}

const PackageRecord* active_record_for(const char* path) {
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (record.used && record.active.valid && same_storage_path(path, record.active.entrypoint)) return &record;
    }
    return nullptr;
}

bool allow_execution(const char* path, const uint8_t* data, uint32_t size) {
    if (!initialized || !path || !data || !size) return false;
    const PackageRecord* record = active_record_for(path);
    if (!record || !authorized_reference(record->name, record->active)) return false;
    security_guard::ScanResult scan = security_guard::scan_memory(path, data, size, false);
    if (scan.verdict != security_guard::Verdict::clean ||
        !security_guard::digest_equal(scan.digest, record->active.payload_digest)) return false;
    scan.verdict = security_guard::Verdict::trusted;
    storage::path_copy(scan.signature, "ZenRepo.SignedTarget", sizeof(scan.signature));
    if (!security_guard::record(security_guard::AuditAction::execute, scan)) return false;
    serial::write("ZENPKG_EXEC_ALLOWED name=");
    serial::write(record->name);
    serial::write(" version=");
    serial::line(record->active.version);
    return true;
}

bool activate_capabilities(const char* path, const uint8_t* data, uint32_t size) {
    const PackageRecord* record = active_record_for(path);
    if (!record || !authorized_reference(record->name, record->active)) return false;
    const AuthorizedPackage* trusted = find_authorized(
        record->name, record->active.version, record->active.entrypoint, record->active.payload_type);
    if (!trusted) return false;
    uint8_t digest[32]{};
    crypto::sha256(data, size, digest);
    if (!crypto::digest_equal(digest, trusted->payload_digest)) return false;
    return process::activate_external_capability_profile(
        path, digest, trusted->syscall_mask, trusted->read_scope, trusted->write_scope);
}

bool target_package_path(const package_repository::Target& target, char output[48]) {
    const char* basename = target.path;
    for (const char* cursor = target.path; *cursor; ++cursor) {
        if (*cursor == '/') basename = cursor + 1;
    }
    if (!*basename || !copy_text_exact(output, 48U, "/data/packages/")) return false;
    return native_package::append_text(output, 48U, basename);
}

bool install_latest(const char* name) {
    const package_repository::Target* target = package_repository::latest_target(name);
    if (!target) {
        console::warning("No signed package with that name.");
        serial::line("ZENPKG_TARGET_NOT_FOUND");
        return false;
    }
    char path[48]{};
    return target_package_path(*target, path) && install(path);
}

bool upgrade_one(const char* name) {
    const package_repository::Target* latest = package_repository::latest_target(name);
    const int32_t index = find_record(name);
    if (!latest || index < 0) {
        console::warning("Installed package or signed target not found.");
        return false;
    }
    const int order = version_compare(latest->version, database.records[index].active.version);
    if (order <= 0) {
        serial::line(order == 0 ? "ZENPKG_UPGRADE_CURRENT" : "ZENPKG_UPGRADE_REPOSITORY_BEHIND");
        return order == 0;
    }
    char path[48]{};
    return target_package_path(*latest, path) && install(path);
}

bool upgrade(char* name) {
    lower_in_place(name);
    if (*name && !string_equal(name, "all")) return upgrade_one(name);
    char names[maximum_packages][32]{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (!database.records[i].used) continue;
        storage::path_copy(names[count++], database.records[i].name, sizeof(names[0]));
    }
    bool okay = true;
    for (uint32_t i = 0; i < count; ++i) {
        if (!upgrade_one(names[i])) okay = false;
    }
    serial::line(okay ? "ZENPKG_UPGRADE_ALL_OK" : "ZENPKG_UPGRADE_ALL_PARTIAL");
    return okay;
}

bool repair_one(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) {
        serial::line("ZENPKG_REPAIR_NOT_INSTALLED");
        return false;
    }
    PackageRecord& record = database.records[index];
    if (reference_payload_valid(record.name, record.active)) {
        serial::line("ZENPKG_REPAIR_HEALTHY");
        return true;
    }
    if (record.previous.valid && reference_payload_valid(record.name, record.previous)) {
        char package_name[32]{};
        storage::path_copy(package_name, record.name, sizeof(package_name));
        serial::line("ZENPKG_REPAIR_ROLLBACK");
        return rollback(package_name);
    }
    console::warning("No trusted payload is available; package remains fail-closed.");
    serial::line("ZENPKG_REPAIR_FAILED_CLOSED");
    return false;
}

bool repair(char* name) {
    lower_in_place(name);
    if (*name && !string_equal(name, "all")) return repair_one(name);
    bool okay = true;
    uint32_t count = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (!database.records[i].used) continue;
        ++count;
        if (!repair_one(database.records[i].name)) okay = false;
    }
    if (!count) console::line("  <no installed packages>");
    serial::line(okay ? "ZENPKG_REPAIR_ALL_OK" : "ZENPKG_REPAIR_ALL_FAILED");
    return okay;
}

bool run_package(char* input) {
    while (input && is_space(*input)) ++input;
    if (!input || !*input) {
        console::warning("Usage: pkg run <name> [arguments]");
        return false;
    }
    char* remainder = input;
    while (*remainder && !is_space(*remainder)) ++remainder;
    if (*remainder) *remainder++ = 0;
    while (is_space(*remainder)) ++remainder;
    lower_in_place(input);
    const int32_t index = find_record(input);
    if (index < 0) {
        console::warning("Package is not installed.");
        serial::line("ZENPKG_NOT_INSTALLED");
        return false;
    }
    char launch[zenov_generated::kShellLineCapacity]{};
    if (!copy_text_exact(launch, sizeof(launch), database.records[index].active.entrypoint)) return false;
    if (*remainder && (!native_package::append_text(launch, sizeof(launch), " ") ||
        !native_package::append_text(launch, sizeof(launch), remainder))) return false;
    serial::write("ZENPKG_RUN name=");
    serial::write(input);
    serial::write(" version=");
    serial::line(database.records[index].active.version);
    return process::run_unchecked(launch);
}
