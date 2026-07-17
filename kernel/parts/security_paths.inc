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
};

bool protected_path(const char* input) {
    if (!input || !*input) return false;
    char normalized[48];
    if (!storage::normalize_path(input, normalized)) return false;
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
} // namespace storage
