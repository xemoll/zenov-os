namespace security_policy {
static constexpr const char* protected_paths[] = {
    "/apps/hello.zex",
    "/apps/fileio.elf",
    "/apps/args.elf",
    "/apps/console.elf",
    "/apps/protect.elf",
    "/apps/kaccess.elf",
    "/apps/zenovapp.zex",
    "/security/zenovguard.zgdb",
    "/security/zenovguard.version",
    "/security/zenovguard.audit",
};

bool normalize_for_policy(const char* input, char output[48]) {
    return input && *input && storage::normalize_path(input, output);
}

bool package_payload_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_starts_with(normalized, "/apps/pkg-");
}

bool package_database_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_equal(normalized, "/var/lib/zenpkg/state.v1");
}

bool repository_state_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_equal(normalized, "/var/lib/zenpkg/repo.v1");
}

bool repository_metadata_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_starts_with(normalized, "/repo/");
}

bool package_managed_path(const char* input) {
    return package_payload_path(input) || package_database_path(input) || repository_state_path(input);
}

bool protected_path(const char* input) {
    if (!input || !*input) return false;
    char normalized[48]{};
    if (!normalize_for_policy(input, normalized)) return false;
    if (string_starts_with(normalized, "/apps/pkg-") || string_starts_with(normalized, "/repo/") ||
        string_equal(normalized, "/var/lib/zenpkg/state.v1") || string_equal(normalized, "/var/lib/zenpkg/repo.v1")) return true;
    for (const char* path : protected_paths) if (string_equal(normalized, path)) return true;
    return false;
}

void report_protected_path(const char* operation, const char* path) {
    serial::write("ZENOV_GUARD_PROTECTED_PATH_BLOCKED operation=");
    serial::write(operation);
    serial::write(" path=");
    serial::line(path ? path : "<null>");
}
} // namespace security_policy

namespace storage {
bool guarded_write_file(const char* path, const uint8_t* data, uint32_t size, bool append) {
    if (security_policy::protected_path(path)) {
        security_policy::report_protected_path("write", path);
        return false;
    }
    return write_file(path, data, size, append);
}

bool guarded_remove(const char* path) {
    if (security_policy::protected_path(path)) {
        security_policy::report_protected_path("remove", path);
        return false;
    }
    return remove(path);
}

bool guarded_rename_entry(const char* source, const char* destination) {
    if (security_policy::protected_path(source) || security_policy::protected_path(destination)) {
        security_policy::report_protected_path("rename", security_policy::protected_path(source) ? source : destination);
        return false;
    }
    return rename_entry(source, destination);
}

bool guarded_copy_file(const char* source, const char* destination) {
    if (security_policy::protected_path(destination)) {
        security_policy::report_protected_path("copy-destination", destination);
        return false;
    }
    return copy_file(source, destination);
}

bool package_write_file(const char* path, const uint8_t* data, uint32_t size, bool append) {
    if (append || !security_policy::package_managed_path(path)) {
        security_policy::report_protected_path("package-write-rejected", path);
        return false;
    }
    return write_file(path, data, size, false);
}

bool package_remove(const char* path) {
    if (!security_policy::package_payload_path(path)) {
        security_policy::report_protected_path("package-remove-rejected", path);
        return false;
    }
    return remove(path);
}
} // namespace storage
