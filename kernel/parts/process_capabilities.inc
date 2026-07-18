namespace process {
constexpr uint32_t capability_console_write = 1U << 0U;
constexpr uint32_t capability_ticks = 1U << 1U;
constexpr uint32_t capability_file_read = 1U << 2U;
constexpr uint32_t capability_file_write = 1U << 3U;
constexpr uint32_t capability_file_stat = 1U << 4U;
constexpr uint32_t capability_version = 1U << 5U;
constexpr uint32_t capability_sync = 1U << 6U;
constexpr uint32_t capability_console_read = 1U << 7U;
constexpr uint32_t capability_all = capability_console_write | capability_ticks | capability_file_read | capability_file_write |
    capability_file_stat | capability_version | capability_sync | capability_console_read;
constexpr uint32_t syscall_error_denied = 0xFFFFFFF9U;

struct CapabilityProfile {
    const char* path;
    uint32_t mask;
    const char* read_scope;
    const char* write_scope;
};

static constexpr CapabilityProfile capability_profiles[] = {
    {"/apps/hello.zex", capability_console_write, "", ""},
    {"/apps/fileio.elf", capability_console_write | capability_file_read | capability_file_write | capability_file_stat | capability_version | capability_sync,
        "/apps/userio.txt", "/apps/userio.txt"},
    {"/apps/args.elf", capability_console_write | capability_file_read, "/docs/readme.txt", ""},
    {"/apps/console.elf", capability_console_write | capability_console_read, "", ""},
    {"/apps/protect.elf", capability_console_write, "", ""},
    {"/apps/kaccess.elf", 0U, "", ""},
    {"/apps/zenovapp.zex", capability_console_write, "", ""},
};

struct ActiveCapabilityProfile {
    bool valid = false;
    uint32_t mask = 0;
    char path[48]{};
    char read_scope[48]{};
    char write_scope[48]{};
    uint8_t digest[security_guard::sha256_bytes]{};
};

ActiveCapabilityProfile active_capabilities{};
uint32_t capability_denials = 0, scope_denials = 0;
bool capability_ready = false, capability_audit_failed = false;

const char* capability_name(uint32_t capability) {
    switch (capability) {
        case capability_console_write: return "console-write";
        case capability_ticks: return "ticks";
        case capability_file_read: return "file-read";
        case capability_file_write: return "file-write";
        case capability_file_stat: return "file-stat";
        case capability_version: return "version";
        case capability_sync: return "sync";
        case capability_console_read: return "console-read";
        default: return "unknown";
    }
}

uint32_t syscall_capability(uint32_t number) {
    switch (number) {
        case syscall_console_write: return capability_console_write;
        case syscall_ticks: return capability_ticks;
        case syscall_file_read: return capability_file_read;
        case syscall_file_write: return capability_file_write;
        case syscall_file_stat: return capability_file_stat;
        case syscall_version: return capability_version;
        case syscall_sync: return capability_sync;
        case syscall_console_read: return capability_console_read;
        default: return 0U;
    }
}

const CapabilityProfile* find_capability_profile(const char* normalized_path) {
    if (!normalized_path) return nullptr;
    for (const auto& profile : capability_profiles) if (string_equal(profile.path, normalized_path)) return &profile;
    return nullptr;
}

bool normalize_capability_path(const char* input, char output[48]) {
    return input && storage::normalize_path(input, output);
}

bool capability_profile_available(const char* input) {
    if (!capability_ready) {
        serial::line("SYSCALL_CAPABILITY_POLICY_NOT_READY");
        return false;
    }
    char normalized[48];
    if (!normalize_capability_path(input, normalized) || !find_capability_profile(normalized)) {
        serial::write("SYSCALL_CAPABILITY_PROFILE_MISSING path="); serial::line(input ? input : "<null>");
        return false;
    }
    return true;
}

bool activate_capability_profile(const char* input, const uint8_t* data, uint32_t size) {
    if (!capability_profile_available(input)) return false;
    char normalized[48];
    if (!normalize_capability_path(input, normalized)) return false;
    const CapabilityProfile* profile = find_capability_profile(normalized);
    if (!profile || (!data && size)) return false;
    memset(&active_capabilities, 0, sizeof(active_capabilities));
    active_capabilities.valid = true;
    active_capabilities.mask = profile->mask;
    storage::path_copy(active_capabilities.path, profile->path, sizeof(active_capabilities.path));
    storage::path_copy(active_capabilities.read_scope, profile->read_scope, sizeof(active_capabilities.read_scope));
    storage::path_copy(active_capabilities.write_scope, profile->write_scope, sizeof(active_capabilities.write_scope));
    security_guard::sha256(data, size, active_capabilities.digest);
    capability_audit_failed = false;
    serial::write("SYSCALL_CAPABILITY_PROFILE_ACTIVE app="); serial::write(active_capabilities.path);
    serial::write(" mask="); serial_hex(active_capabilities.mask); serial::write("\r\n");
    return true;
}

void clear_capability_profile() {
    memset(&active_capabilities, 0, sizeof(active_capabilities));
    capability_audit_failed = false;
}

bool exact_scope_allows(const char* path, const char* scope) {
    if (!path || !scope || !*scope) return false;
    char normalized[48];
    return storage::normalize_path(path, normalized) && string_equal(normalized, scope);
}

bool audit_capability_denial(uint32_t number, uint32_t required, const char* reason) {
    security_guard::ScanResult result{};
    result.verdict = security_guard::Verdict::untrusted;
    result.executable = true;
    result.size = number;
    storage::path_copy(result.path, active_capabilities.valid ? active_capabilities.path : "<no-profile>", sizeof(result.path));
    storage::path_copy(result.signature, reason, sizeof(result.signature));
    if (active_capabilities.valid) memcpy(result.digest, active_capabilities.digest, sizeof(result.digest));
    const bool recorded = security_guard::record(security_guard::AuditAction::execute, result);
    serial::write("SYSCALL_CAPABILITY_DENIED app="); serial::write(result.path);
    serial::write(" syscall="); serial_u32(number); serial::write(" capability="); serial::write(capability_name(required));
    serial::write(" reason="); serial::line(reason);
    if (!recorded) {
        capability_audit_failed = true;
        serial::line("SYSCALL_CAPABILITY_AUDIT_FAILED");
    }
    return recorded;
}

bool syscall_capability_allowed(uint32_t number) {
    const uint32_t required = syscall_capability(number);
    if (!required) return true;
    if (active_capabilities.valid && (active_capabilities.mask & required) == required) return true;
    ++capability_denials;
    (void)audit_capability_denial(number, required, "missing-capability");
    return false;
}

bool syscall_scope_allowed(uint32_t number, const char* path) {
    const uint32_t required = syscall_capability(number);
    const char* scope = nullptr;
    if (number == syscall_file_read || number == syscall_file_stat) scope = active_capabilities.read_scope;
    else if (number == syscall_file_write) scope = active_capabilities.write_scope;
    else return true;
    if (active_capabilities.valid && exact_scope_allows(path, scope)) return true;
    ++scope_denials;
    (void)audit_capability_denial(number, required, "path-scope");
    return false;
}

bool trusted_profile_bijection() {
    constexpr uint32_t trusted_count = sizeof(security_guard::trusted_executables) / sizeof(security_guard::trusted_executables[0]);
    constexpr uint32_t profile_count = sizeof(capability_profiles) / sizeof(capability_profiles[0]);
    if (trusted_count != profile_count) return false;
    for (const auto& profile : capability_profiles) {
        if (!profile.path || !*profile.path || (profile.mask & ~capability_all)) return false;
        uint32_t matches = 0;
        for (const auto& trusted : security_guard::trusted_executables) if (string_equal(profile.path, trusted.path)) ++matches;
        if (matches != 1U) return false;
        const bool needs_read_scope = (profile.mask & (capability_file_read | capability_file_stat)) != 0;
        const bool needs_write_scope = (profile.mask & capability_file_write) != 0;
        if (needs_read_scope != (profile.read_scope && *profile.read_scope)) return false;
        if (needs_write_scope != (profile.write_scope && *profile.write_scope)) return false;
    }
    for (const auto& trusted : security_guard::trusted_executables) if (!find_capability_profile(trusted.path)) return false;
    return true;
}

bool capability_self_test() {
    if (!trusted_profile_bijection()) return false;
    if (syscall_capability(syscall_console_write) != capability_console_write || syscall_capability(255U) != 0U) return false;
    if (!exact_scope_allows("/data/apps/userio.txt", "/apps/userio.txt")) return false;
    if (exact_scope_allows("/data/apps/other.txt", "/apps/userio.txt")) return false;
    const CapabilityProfile* fileio = find_capability_profile("/apps/fileio.elf");
    const CapabilityProfile* args = find_capability_profile("/apps/args.elf");
    const CapabilityProfile* kaccess = find_capability_profile("/apps/kaccess.elf");
    if (!fileio || !args || !kaccess || kaccess->mask != 0U) return false;
    if ((fileio->mask & (capability_file_read | capability_file_write | capability_sync)) !=
        (capability_file_read | capability_file_write | capability_sync)) return false;
    if ((args->mask & capability_file_read) == 0 || (args->mask & capability_file_write) != 0) return false;
    return true;
}

bool capability_init() {
    capability_ready = capability_self_test();
    serial::line(capability_ready ? "SYSCALL_CAPABILITY_POLICY_OK" : "SYSCALL_CAPABILITY_POLICY_FAILED");
    if (capability_ready) {
        serial::write("SYSCALL_CAPABILITY_PROFILES_OK count=");
        serial_u32(static_cast<uint32_t>(sizeof(capability_profiles) / sizeof(capability_profiles[0])));
        serial::write("\r\n");
    }
    return capability_ready;
}

void print_capability_status() {
    console::write("  Syscall policy "); console::line(capability_ready ? "least-privilege profiles enforced" : "unavailable / execution locked");
    console::write("  Profiles       "); console::unsigned_dec(static_cast<uint32_t>(sizeof(capability_profiles) / sizeof(capability_profiles[0]))); console::put('\n');
    console::write("  Cap denials    "); console::unsigned_dec(capability_denials); console::put('\n');
    console::write("  Scope denials  "); console::unsigned_dec(scope_denials); console::put('\n');
}

void print_capability_profiles() {
    for (const auto& profile : capability_profiles) {
        console::write("  "); console::line(profile.path);
        console::write("    mask         "); console::hex(profile.mask); console::put('\n');
        console::write("    read scope   "); console::line(profile.read_scope && *profile.read_scope ? profile.read_scope : "<none>");
        console::write("    write scope  "); console::line(profile.write_scope && *profile.write_scope ? profile.write_scope : "<none>");
    }
    serial::line("SYSCALL_CAPABILITY_LIST_OK");
}

extern "C" uint32_t syscall_dispatch(SyscallFrame* frame) {
    if (!frame) return 1U;
    const uint32_t number = frame->eax;
    if (number == syscall_exit || syscall_capability(number) == 0U) return syscall_dispatch_unrestricted(frame);
    if (!syscall_capability_allowed(number)) {
        frame->eax = syscall_error_denied;
        if (capability_audit_failed) { last_exit_code = 126U; return 1U; }
        return 0U;
    }
    if (number == syscall_file_read || number == syscall_file_write || number == syscall_file_stat) {
        const char* path = user_string(frame->ebx);
        if (path && !syscall_scope_allowed(number, path)) {
            frame->eax = syscall_error_denied;
            if (capability_audit_failed) { last_exit_code = 126U; return 1U; }
            return 0U;
        }
    }
    return syscall_dispatch_unrestricted(frame);
}
} // namespace process
