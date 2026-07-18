bool parse_dependency(const char* requirement, char name[32], char version[16]) {
    const char* separator = requirement;
    while (*separator && *separator != '@') ++separator;
    if (*separator != '@' || separator == requirement || !separator[1]) return false;
    const uint32_t name_size = static_cast<uint32_t>(separator - requirement);
    const uint32_t version_size = static_cast<uint32_t>(string_length(separator + 1));
    if (name_size >= 32U || version_size >= 16U) return false;
    memcpy(name, requirement, name_size); name[name_size] = 0;
    memcpy(version, separator + 1, version_size + 1U);
    return true;
}

bool requirements_satisfied(const native_package::Manifest& manifest, const char*& reason) {
    for (uint32_t i = 0; i < manifest.dependency_count; ++i) {
        char dependency_name[32]{}, dependency_version[16]{};
        if (!parse_dependency(manifest.dependencies[i], dependency_name, dependency_version)) {
            reason = "invalid exact dependency";
            return false;
        }
        const int32_t index = find_record(dependency_name);
        if (index < 0 || !string_equal(database.records[index].active.version, dependency_version)) {
            reason = manifest.dependencies[i];
            return false;
        }
    }
    for (uint32_t i = 0; i < manifest.conflict_count; ++i) {
        if (find_record(manifest.conflicts[i]) >= 0) {
            reason = manifest.conflicts[i];
            return false;
        }
    }
    reason = nullptr;
    return true;
}

bool payload_committed(const native_package::VerifiedPackage& package) {
    storage::FileInfo info{};
    const uint32_t expected_checksum = storage::fnv1a(package.payload, package.payload_size);
    if (storage::query(package.manifest.entrypoint, info)) {
        return info.type == storage::zfs_type_file && info.size == package.payload_size && info.checksum == expected_checksum;
    }
    if (!storage::package_write_file(package.manifest.entrypoint, package.payload, package.payload_size, false)) return false;
    if (!storage::query(package.manifest.entrypoint, info)) return false;
    return info.type == storage::zfs_type_file && info.size == package.payload_size && info.checksum == expected_checksum;
}

bool reference_payload_valid(const char* name, const PackageReference& reference) {
    if (!name || !authorized_reference(name, reference) || !package_buffer) return false;
    storage::FileInfo info{};
    if (!storage::query(reference.entrypoint, info) || info.type != storage::zfs_type_file || info.size > storage::file_capacity()) return false;
    uint32_t size = 0;
    if (!storage::read_file(reference.entrypoint, package_buffer, storage::file_capacity(), size) || size != info.size) return false;
    uint8_t digest[32]{};
    crypto::sha256(package_buffer, size, digest);
    return crypto::digest_equal(digest, reference.payload_digest);
}

bool active_payloads_valid() {
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (database.records[i].used && (!reference_payload_valid(database.records[i].name, database.records[i].active) ||
            (database.records[i].previous.valid && !reference_payload_valid(database.records[i].name, database.records[i].previous)))) return false;
    }
    return true;
}

void print_reference(const char* label, const PackageReference& reference) {
    console::write("  "); console::write(label); console::write("      ");
    if (!reference.valid) { console::line("<none>"); return; }
    console::write(reference.version); console::write("  "); console::line(reference.entrypoint);
}
