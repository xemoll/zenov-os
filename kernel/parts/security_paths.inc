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
    "/security/syscall-capabilities.zcap",
    "/security/syscall-capabilities.version",
    "/security/zenovguard-intelligence.zmid",
    "/security/zenovguard-intelligence.version",
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

bool package_transport_state_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_equal(normalized, "/var/lib/zenpkg/transport.v1");
}

bool repository_metadata_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_starts_with(normalized, "/repo/");
}

bool package_cache_object_path(const char* input) {
    char normalized[48]{};
    return normalize_for_policy(input, normalized) && string_starts_with(normalized, "/var/cache/zp/") &&
        normalized[14] != 0;
}

bool package_cache_partial_path(const char* input) {
    char normalized[48]{};
    if (!normalize_for_policy(input, normalized) || !string_starts_with(normalized, "/var/cache/zp/")) return false;
    const uint32_t length = static_cast<uint32_t>(string_length(normalized));
    const char suffix[] = ".part";
    const uint32_t suffix_length = sizeof(suffix) - 1U;
    if (length <= 14U + suffix_length) return false;
    return storage::bytes_equal(normalized + length - suffix_length, suffix, suffix_length);
}

bool package_managed_path(const char* input) {
    return package_payload_path(input) || package_database_path(input) || repository_state_path(input) ||
        package_transport_state_path(input) || package_cache_object_path(input);
}

bool protected_path(const char* input) {
    if (!input || !*input) return false;
    char normalized[48]{};
    if (!normalize_for_policy(input, normalized)) return false;
    if (string_starts_with(normalized, "/apps/pkg-") || string_starts_with(normalized, "/repo/") ||
        string_equal(normalized, "/var/lib/zenpkg/state.v1") || string_equal(normalized, "/var/lib/zenpkg/repo.v1") ||
        string_equal(normalized, "/var/lib/zenpkg/transport.v1") ||
        string_equal(normalized, "/var/cache/zp") || string_starts_with(normalized, "/var/cache/zp/") ||
        string_equal(normalized, "/quarantine") || string_starts_with(normalized, "/quarantine/")) return true;
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
    if (!security_guard::allow_persistent_write(path, data, size, append)) return false;
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
    if (!security_guard::allow_persistent_transfer(source, destination)) return false;
    return rename_entry(source, destination);
}

bool guarded_copy_file(const char* source, const char* destination) {
    if (security_policy::protected_path(destination)) {
        security_policy::report_protected_path("copy-destination", destination);
        return false;
    }
    if (!security_guard::allow_persistent_transfer(source, destination)) return false;
    return copy_file(source, destination);
}

bool package_write_file(const char* path, const uint8_t* data, uint32_t size, bool append) {
    if (append || !security_policy::package_managed_path(path)) {
        security_policy::report_protected_path("package-write-rejected", path);
        return false;
    }
    if (!security_guard::allow_persistent_write(path, data, size, false)) return false;
    return write_file(path, data, size, false);
}

bool package_cache_write_partial(const char* path, const uint8_t* data, uint32_t size, bool append) {
    if (!security_policy::package_cache_partial_path(path)) {
        security_policy::report_protected_path("package-cache-partial-write-rejected", path);
        return false;
    }
    if (!security_guard::allow_persistent_write(path, data, size, append)) return false;
    return write_file(path, data, size, append);
}

bool package_remove(const char* path) {
    if (!security_policy::package_payload_path(path)) {
        security_policy::report_protected_path("package-remove-rejected", path);
        return false;
    }
    return remove(path);
}

bool package_transport_remove(const char* path) {
    if (!security_policy::package_transport_state_path(path)) {
        security_policy::report_protected_path("package-transport-remove-rejected", path);
        return false;
    }
    return remove(path);
}

bool package_cache_remove(const char* path) {
    if (!security_policy::package_cache_object_path(path)) {
        security_policy::report_protected_path("package-cache-remove-rejected", path);
        return false;
    }
    return remove(path);
}

bool package_cache_rename(const char* source, const char* destination) {
    if (!security_policy::package_cache_object_path(source) || !security_policy::package_cache_object_path(destination)) {
        security_policy::report_protected_path("package-cache-rename-rejected",
            security_policy::package_cache_object_path(source) ? destination : source);
        return false;
    }
    if (!security_guard::allow_persistent_transfer(source, destination)) return false;
    return rename_entry(source, destination);
}

bool quarantine_rename(const char* source, const char* destination) {
    char normalized_source[48]{}, normalized_destination[48]{};
    if (!security_policy::normalize_for_policy(source, normalized_source) ||
        !security_policy::normalize_for_policy(destination, normalized_destination) ||
        security_policy::protected_path(normalized_source) || !string_starts_with(normalized_destination, "/quarantine/")) {
        security_policy::report_protected_path("quarantine-rename-rejected", source);
        return false;
    }
    return rename_entry(normalized_source, normalized_destination);
}

bool quarantine_restore(const char* source, const char* destination) {
    char normalized_source[48]{}, normalized_destination[48]{};
    if (!security_policy::normalize_for_policy(source, normalized_source) ||
        !security_policy::normalize_for_policy(destination, normalized_destination) ||
        !string_starts_with(normalized_source, "/quarantine/") || security_policy::protected_path(normalized_destination)) {
        security_policy::report_protected_path("quarantine-restore-rejected", destination);
        return false;
    }
    return rename_entry(normalized_source, normalized_destination);
}

bool quarantine_write_metadata(const char* path, const uint8_t* data, uint32_t size) {
    char normalized[48]{};
    if (!security_policy::normalize_for_policy(path, normalized) || !string_starts_with(normalized, "/quarantine/") ||
        !string_starts_with(normalized + (string_length(normalized) > 5U ? string_length(normalized) - 5U : 0U), ".meta")) {
        security_policy::report_protected_path("quarantine-metadata-rejected", path);
        return false;
    }
    return write_file(normalized, data, size, false);
}

bool quarantine_remove(const char* path) {
    char normalized[48]{};
    if (!security_policy::normalize_for_policy(path, normalized) || !string_starts_with(normalized, "/quarantine/")) {
        security_policy::report_protected_path("quarantine-remove-rejected", path);
        return false;
    }
    return remove(normalized);
}
} // namespace storage
