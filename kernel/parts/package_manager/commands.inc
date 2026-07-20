bool contains_folded(const char* text, const char* query) {
    if (!text || !query || !*query) return false;
    for (const char* start = text; *start; ++start) {
        const char* left = start;
        const char* right = query;
        while (*left && *right && ascii_lower(*left) == ascii_lower(*right)) { ++left; ++right; }
        if (!*right) return true;
    }
    return false;
}

void list() {
    console::line("  NAME                             ACTIVE          PREVIOUS");
    console::line("  -------------------------------- --------------- ---------------");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (!record.used) continue;
        console::write("  "); console::write(record.name);
        uint32_t width = static_cast<uint32_t>(string_length(record.name));
        while (width++ < 33U) console::put(' ');
        console::write(record.active.version);
        width = static_cast<uint32_t>(string_length(record.active.version));
        while (width++ < 16U) console::put(' ');
        console::line(record.previous.valid ? record.previous.version : "-");
        ++shown;
    }
    if (!shown) console::line("  <no installed packages>");
    serial::line("ZENPKG_LIST_OK");
}

void info(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) { console::warning("PKG_NOT_INSTALLED"); serial::line("ZENPKG_NOT_INSTALLED"); return; }
    const PackageRecord& record = database.records[index];
    console::write("  Name          "); console::line(record.name);
    print_reference("Active", record.active);
    print_reference("Previous", record.previous);
    console::write("  DB generation "); console::unsigned_dec(database.generation); console::put('\n');
    serial::write("ZENPKG_INFO name="); serial::write(record.name);
    serial::write(" version="); serial::line(record.active.version);
}

void status() {
    console::write("  State         "); console::line(initialized ? "ready" : "unavailable");
    console::write("  Installed     "); console::unsigned_dec(database.count); console::put('\n');
    console::write("  Authorized    "); console::unsigned_dec(authorized_package_count()); console::line(" signed targets");
    console::line("  Integrity     canonical package + package/payload SHA-256");
    console::line("  Repository    signed roles, expiry, delegation and anti-rollback");
    console::line("  Syscalls      signed least-privilege masks and exact scopes");
    console::line("  Transaction   immutable payload + atomic active-reference commit");
    console::line("  Operations    search, plan, install, upgrade, repair, rollback and remove");
    console::line("  Transport     offline; network download remains disabled");
    serial::line("ZENPKG_STATUS_OK");
}

bool verify(const char* path) {
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(path, package_buffer,
        storage::file_capacity(), package, package_size);
    if (error != native_package::Error::none) {
        console::write("Package verification failed: "); console::line(native_package::error_text(error));
        serial::line("ZENPKG_VERIFY_REJECTED"); return false;
    }
    const bool authorized = authorized_package(package, package_size);
    console::write("  Name          "); console::line(package.manifest.name);
    console::write("  Version       "); console::line(package.manifest.version);
    console::write("  Entrypoint    "); console::line(package.manifest.entrypoint);
    console::write("  Package bytes "); console::unsigned_dec(package_size); console::put('\n');
    console::write("  Payload bytes "); console::unsigned_dec(package.payload_size); console::put('\n');
    console::write("  Authorization "); console::line(authorized ? "signed target" : "not authorized");
    console::success("PKG_VERIFY_OK");
    serial::line(authorized ? "ZENPKG_VERIFY_AUTHORIZED" : "ZENPKG_VERIFY_UNAUTHORIZED");
    serial::line("ZENPKG_VERIFY_OK"); return true;
}

bool same_storage_path(const char* left, const char* right) {
    char a[48]{}, b[48]{};
    return left && right && storage::normalize_path(left, a) && storage::normalize_path(right, b) && string_equal(a, b);
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
    serial::write("ZENPKG_EXEC_ALLOWED name="); serial::write(record->name);
    serial::write(" version="); serial::line(record->active.version);
    return true;
}
bool activate_capabilities(const char* path, const uint8_t* data, uint32_t size) {
    const PackageRecord* record = active_record_for(path);
    if (!record || !authorized_reference(record->name, record->active)) return false;
    const AuthorizedPackage* trusted = find_authorized(record->name, record->active.version,
        record->active.entrypoint, record->active.payload_type);
    if (!trusted) return false;
    uint8_t digest[32]{}; crypto::sha256(data, size, digest);
    if (!crypto::digest_equal(digest, trusted->payload_digest)) return false;
    return process::activate_external_capability_profile(path, digest, trusted->syscall_mask,
        trusted->read_scope, trusted->write_scope);
}

bool target_package_path(const package_repository::Target& target, char output[48]) {
    const char* basename = target.path;
    for (const char* cursor = target.path; *cursor; ++cursor) if (*cursor == '/') basename = cursor + 1;
    if (!*basename || !copy_text_exact(output, 48U, "/data/packages/")) return false;
    return native_package::append_text(output, 48U, basename);
}

bool install_latest(const char* name) {
    const package_repository::Target* target = package_repository::latest_target(name);
    if (!target) { console::warning("No signed package with that name."); serial::line("ZENPKG_TARGET_NOT_FOUND"); return false; }
    char path[48]{};
    if (!target_package_path(*target, path)) return false;
    return install(path);
}

void search(const char* query) {
    package_repository::print_search(query);
    serial::line("ZENPKG_SEARCH_OK");
}

void plan_one(const char* name) {
    const package_repository::Target* latest = package_repository::latest_target(name);
    const int32_t index = find_record(name);
    if (!latest) {
        console::write("  "); console::write(name); console::line("  unavailable in signed repository");
        serial::line("ZENPKG_PLAN_UNAVAILABLE"); return;
    }
    if (index < 0) {
        console::write("  install "); console::write(latest->name); console::put('@'); console::line(latest->version);
        serial::line("ZENPKG_PLAN_INSTALL"); return;
    }
    const int order = version_compare(latest->version, database.records[index].active.version);
    console::write("  "); console::write(name); console::write("  ");
    if (order > 0) {
        console::write(database.records[index].active.version); console::write(" -> "); console::line(latest->version);
        serial::line("ZENPKG_PLAN_UPGRADE");
    } else if (order == 0) {
        console::line("current"); serial::line("ZENPKG_PLAN_CURRENT");
    } else {
        console::line("installed version is newer than repository"); serial::line("ZENPKG_PLAN_REPOSITORY_BEHIND");
    }
}

void plan(char* name) {
    lower_in_place(name);
    if (!*name || string_equal(name, "all")) {
        uint32_t shown = 0;
        for (uint32_t i = 0; i < maximum_packages; ++i) if (database.records[i].used) { plan_one(database.records[i].name); ++shown; }
        if (!shown) console::line("  <no installed packages>");
    } else plan_one(name);
    serial::line("ZENPKG_PLAN_OK");
}

bool upgrade_one(const char* name) {
    const package_repository::Target* latest = package_repository::latest_target(name);
    const int32_t index = find_record(name);
    if (!latest || index < 0) { console::warning("Installed package or signed target not found."); return false; }
    const int order = version_compare(latest->version, database.records[index].active.version);
    if (order <= 0) { serial::line(order == 0 ? "ZENPKG_UPGRADE_CURRENT" : "ZENPKG_UPGRADE_REPOSITORY_BEHIND"); return order == 0; }
    char path[48]{};
    return target_package_path(*latest, path) && install(path);
}

bool upgrade(char* name) {
    lower_in_place(name);
    if (*name && !string_equal(name, "all")) return upgrade_one(name);
    char names[maximum_packages][32]{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) if (database.records[i].used) {
        storage::path_copy(names[count++], database.records[i].name, sizeof(names[0]));
    }
    bool okay = true;
    for (uint32_t i = 0; i < count; ++i) if (!upgrade_one(names[i])) okay = false;
    serial::line(okay ? "ZENPKG_UPGRADE_ALL_OK" : "ZENPKG_UPGRADE_ALL_PARTIAL");
    return okay;
}

bool repair_one(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) { serial::line("ZENPKG_REPAIR_NOT_INSTALLED"); return false; }
    PackageRecord& record = database.records[index];
    if (reference_payload_valid(record.name, record.active)) { serial::line("ZENPKG_REPAIR_HEALTHY"); return true; }
    if (record.previous.valid && reference_payload_valid(record.name, record.previous)) {
        char package_name[32]{};
        storage::path_copy(package_name, record.name, sizeof(package_name));
        serial::line("ZENPKG_REPAIR_ROLLBACK"); return rollback(package_name);
    }
    console::warning("No trusted payload is available; package remains fail-closed.");
    serial::line("ZENPKG_REPAIR_FAILED_CLOSED"); return false;
}

bool repair(char* name) {
    lower_in_place(name);
    if (*name && !string_equal(name, "all")) return repair_one(name);
    bool okay = true; uint32_t count = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) if (database.records[i].used) {
        ++count; if (!repair_one(database.records[i].name)) okay = false;
    }
    if (!count) console::line("  <no installed packages>");
    serial::line(okay ? "ZENPKG_REPAIR_ALL_OK" : "ZENPKG_REPAIR_ALL_FAILED"); return okay;
}

bool run_package(char* input) {
    while (input && is_space(*input)) ++input;
    if (!input || !*input) { console::warning("Usage: pkg run <name> [arguments]"); return false; }
    char* remainder = input; while (*remainder && !is_space(*remainder)) ++remainder;
    if (*remainder) *remainder++ = 0; while (is_space(*remainder)) ++remainder; lower_in_place(input);
    const int32_t index = find_record(input);
    if (index < 0) { console::warning("Package is not installed."); serial::line("ZENPKG_NOT_INSTALLED"); return false; }
    char launch[zenov_generated::kShellLineCapacity]{};
    if (!copy_text_exact(launch, sizeof(launch), database.records[index].active.entrypoint)) return false;
    if (*remainder && (!native_package::append_text(launch, sizeof(launch), " ") ||
        !native_package::append_text(launch, sizeof(launch), remainder))) return false;
    serial::write("ZENPKG_RUN name="); serial::write(input);
    serial::write(" version="); serial::line(database.records[index].active.version);
    return process::run(launch);
}

bool split_subcommand(char* input, char*& action, char*& remainder) {
    if (!input) return false; while (is_space(*input)) ++input; if (!*input) return false;
    action = input; while (*input && !is_space(*input)) ++input;
    if (*input) *input++ = 0; while (is_space(*input)) ++input;
    remainder = input; lower_in_place(action); return true;
}
void command(char* input);
bool command_token_equal(const char* line, const char* expected) {
    if (!line) return false; while (is_space(*line)) ++line;
    while (*expected && *line && !is_space(*line)) { if (ascii_lower(*line++) != *expected++) return false; }
    return !*expected && (!*line || is_space(*line));
}
bool dispatch_line(char* line) {
    if (!command_token_equal(line, "pkg")) return false;
    while (is_space(*line)) ++line; while (*line && !is_space(*line)) ++line; while (is_space(*line)) ++line;
    command(line); return true;
}

void repository_command(char* input) {
    char *action = nullptr, *arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) { console::line("Usage: pkg repo status|targets|search|check|refresh"); return; }
    if (string_equal(action, "status")) package_repository::print_status();
    else if (string_equal(action, "targets")) package_repository::print_targets();
    else if (string_equal(action, "search")) { if (*arguments) package_repository::print_search(arguments); else console::warning("Usage: pkg repo search <query>"); }
    else if (string_equal(action, "check")) { if (package_repository::check()) console::success("PKG_REPOSITORY_CHECK_OK"); else console::warning("Package repository check rejected."); }
    else if (string_equal(action, "refresh")) { if (package_repository::refresh()) console::success("PKG_REPOSITORY_REFRESH_OK"); else console::warning("Package repository refresh rejected."); }
    else console::warning("Unknown pkg repo action.");
}

void command(char* input) {
    char *action = nullptr, *arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg status|list|search|plan|verify|install|upgrade|repair|policy|info|rollback|remove|run|repo"); return;
    }
    if (string_equal(action, "status")) status();
    else if (string_equal(action, "list")) list();
    else if (string_equal(action, "search")) { if (*arguments) search(arguments); else console::warning("Usage: pkg search <query>"); }
    else if (string_equal(action, "plan")) plan(arguments);
    else if (string_equal(action, "verify")) { if (*arguments) verify(arguments); else console::warning("Usage: pkg verify <package.zpk>"); }
    else if (string_equal(action, "install")) {
        if (!*arguments) console::warning("Usage: pkg install <name|package.zpk>");
        else if (arguments[0] == '/' || contains_folded(arguments, ".zpk")) install(arguments);
        else { lower_in_place(arguments); install_latest(arguments); }
    }
    else if (string_equal(action, "upgrade")) upgrade(arguments);
    else if (string_equal(action, "repair")) repair(arguments);
    else if (string_equal(action, "policy")) { lower_in_place(arguments); if (*arguments) package_repository::print_policy(arguments); else console::warning("Usage: pkg policy <name>"); }
    else if (string_equal(action, "info")) { lower_in_place(arguments); if (*arguments) info(arguments); else console::warning("Usage: pkg info <name>"); }
    else if (string_equal(action, "rollback")) { lower_in_place(arguments); if (*arguments) rollback(arguments); else console::warning("Usage: pkg rollback <name>"); }
    else if (string_equal(action, "remove")) { lower_in_place(arguments); if (*arguments) remove_package(arguments); else console::warning("Usage: pkg remove <name>"); }
    else if (string_equal(action, "run")) run_package(arguments);
    else if (string_equal(action, "repo")) repository_command(arguments);
    else console::warning("Unknown pkg action.");
}
