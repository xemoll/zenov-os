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

bool reference_requirements_satisfied(const Database& state, const char* package_name, const PackageReference& reference, const char*& reason) {
    for (uint32_t i = 0; i < reference.dependency_count; ++i) {
        char dependency_name[32]{}, dependency_version[16]{};
        if (!parse_dependency(reference.dependencies[i], dependency_name, dependency_version)) { reason = "invalid exact dependency"; return false; }
        const int32_t index = find_record_in(state, dependency_name);
        if (index < 0 || !string_equal(state.records[index].active.version, dependency_version)) { reason = reference.dependencies[i]; return false; }
    }
    for (uint32_t i = 0; i < reference.conflict_count; ++i) {
        if (!string_equal(reference.conflicts[i], package_name) && find_record_in(state, reference.conflicts[i]) >= 0) { reason = reference.conflicts[i]; return false; }
    }
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (!state.records[i].used || string_equal(state.records[i].name, package_name)) continue;
        for (uint32_t c = 0; c < state.records[i].active.conflict_count; ++c) {
            if (string_equal(state.records[i].active.conflicts[c], package_name)) { reason = state.records[i].name; return false; }
        }
    }
    reason = nullptr;
    return true;
}

bool manifest_requirements_satisfied(const native_package::Manifest& manifest, const char*& reason) {
    PackageReference temporary{};
    temporary.valid = 1U;
    temporary.dependency_count = manifest.dependency_count;
    temporary.conflict_count = manifest.conflict_count;
    for (uint32_t i = 0; i < manifest.dependency_count; ++i) copy_text_exact(temporary.dependencies[i], sizeof(temporary.dependencies[i]), manifest.dependencies[i]);
    for (uint32_t i = 0; i < manifest.conflict_count; ++i) copy_text_exact(temporary.conflicts[i], sizeof(temporary.conflicts[i]), manifest.conflicts[i]);
    return reference_requirements_satisfied(database, manifest.name, temporary, reason);
}

bool same_storage_path(const char* left, const char* right) {
    char a[48]{}, b[48]{};
    return left && right && storage::normalize_path(left, a) && storage::normalize_path(right, b) && string_equal(a, b);
}

bool final_payload_valid(const native_package::VerifiedPackage& package) {
    uint32_t actual = 0;
    if (!storage::read_file(package.manifest.entrypoint, verification_buffer, storage::file_capacity(), actual) || actual != package.payload_size) return false;
    uint8_t digest[32];
    security_guard::sha256(verification_buffer, actual, digest);
    if (!security_guard::digest_equal(digest, package.payload_digest)) return false;
    const security_guard::ScanResult scan = security_guard::scan_memory(package.manifest.entrypoint, verification_buffer, actual, false);
    return scan.verdict == security_guard::Verdict::clean && security_guard::digest_equal(scan.digest, package.payload_digest);
}

bool payload_committed(const native_package::VerifiedPackage& package) {
    storage::FileInfo info{};
    if (storage::query(package.manifest.entrypoint, info)) {
        if (info.type != storage::zfs_type_file || info.size != package.payload_size) return false;
        return final_payload_valid(package);
    }
    if (!storage::write_file(package.manifest.entrypoint, package.payload, package.payload_size, false) || !storage::sync_metadata()) return false;
    return final_payload_valid(package);
}
