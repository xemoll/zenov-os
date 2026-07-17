void list() {
    console::line("  NAME                             ACTIVE          PREVIOUS");
    console::line("  -------------------------------- --------------- ---------------");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (!record.used) continue;
        console::write("  "); console::write(record.name);
        uint32_t width = static_cast<uint32_t>(string_length(record.name)); while (width++ < 33U) console::put(' ');
        console::write(record.active.version); width = static_cast<uint32_t>(string_length(record.active.version)); while (width++ < 16U) console::put(' ');
        console::line(record.previous.valid ? record.previous.version : "-"); ++shown;
    }
    if (!shown) console::line("  <no installed packages>");
    serial::line("ZENPKG_LIST_OK");
}

void print_reference(const char* label, const PackageReference& reference) {
    console::write("  "); console::write(label); console::write("      ");
    if (!reference.valid) { console::line("<none>"); return; }
    console::write(reference.version); console::write("  "); console::line(reference.entrypoint);
}

void info(const char* name) {
    const int32_t index = find_record(name);
    if (index < 0) { console::warning("PKG_NOT_INSTALLED"); serial::line("ZENPKG_NOT_INSTALLED"); return; }
    const PackageRecord& record = database.records[index];
    console::write("  Name          "); console::line(record.name);
    print_reference("Active", record.active); print_reference("Previous", record.previous);
    console::write("  DB generation "); console::unsigned_dec(database.generation); console::put('\n');
    serial::write("ZENPKG_INFO name="); serial::write(record.name); serial::write(" version="); serial::line(record.active.version);
}

void status() {
    console::write("  State         "); console::line(initialized ? "ready" : "unavailable");
    console::write("  Format        "); console::line("ZENPKG1 / signed ZPC2 / ZPKDB1");
    console::write("  Installed     "); console::unsigned_dec(database.count); console::put('\n');
    console::write("  Generation    "); console::unsigned_dec(database.generation); console::put('\n');
    console::line("  Transaction   immutable payload + atomic active-reference commit");
    console::line("  Network       disabled until timestamp/snapshot metadata exists");
    serial::line("ZENPKG_STATUS_OK");
}

bool verify(const char* path) {
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(path, package_buffer, storage::file_capacity(), package, package_size);
    if (error != native_package::Error::none) {
        console::write("Package verification failed: "); console::line(native_package::error_text(error)); serial::line("ZENPKG_VERIFY_REJECTED"); return false;
    }
    const bool authorized = package_catalog::authorizes_package(package, package_size);
    console::write("  Name          "); console::line(package.manifest.name);
    console::write("  Version       "); console::line(package.manifest.version);
    console::write("  Entrypoint    "); console::line(package.manifest.entrypoint);
    console::write("  Catalog       "); console::line(authorized ? "AUTHORIZED" : "NOT AUTHORIZED");
    if (!authorized) { serial::line("ZENPKG_VERIFY_UNAUTHORIZED"); return false; }
    console::success("PKG_VERIFY_OK"); serial::line("ZENPKG_VERIFY_OK"); return true;
}

bool trusted_execution(const char* path, const uint8_t* data, uint32_t size) {
    if (!initialized || !package_catalog::ready || !path || !data || !size) return false;
    const security_guard::ScanResult scan = security_guard::scan_memory(path, data, size, false);
    if (scan.verdict != security_guard::Verdict::clean) return false;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (!record.used || !record.active.valid || !same_storage_path(path, record.active.entrypoint) || size != record.active.payload_size) continue;
        if (!security_guard::digest_equal(scan.digest, record.active.payload_digest) || !reference_catalog_authorized(record.name, record.active)) return false;
        security_guard::record(security_guard::AuditAction::execute, scan);
        serial::write("ZENPKG_EXEC_ALLOWED name="); serial::write(record.name); serial::write(" version="); serial::line(record.active.version);
        return true;
    }
    return false;
}

bool run_package(char* input) {
    while (input && is_space(*input)) ++input;
    if (!input || !*input) { console::warning("Usage: pkg run <name> [arguments]"); return false; }
    char* remainder = input;
    while (*remainder && !is_space(*remainder)) ++remainder;
    if (*remainder) *remainder++ = 0;
    while (is_space(*remainder)) ++remainder;
    lower_in_place(input);
    const int32_t index = find_record(input);
    if (index < 0) { console::warning("Package is not installed."); serial::line("ZENPKG_NOT_INSTALLED"); return false; }
    char launch[zenov_generated::kShellLineCapacity]{};
    if (!copy_text_exact(launch, sizeof(launch), database.records[index].active.entrypoint)) return false;
    if (*remainder) {
        uint32_t length = static_cast<uint32_t>(string_length(launch));
        if (length + 1U >= sizeof(launch)) return false;
        launch[length++] = ' '; launch[length] = 0;
        if (!native_package::append_text(launch, sizeof(launch), remainder)) return false;
    }
    serial::write("ZENPKG_RUN name="); serial::write(input); serial::write(" version="); serial::line(database.records[index].active.version);
    return process::run(launch);
}

bool split_subcommand(char* input, char*& action, char*& remainder) {
    if (!input) return false;
    while (is_space(*input)) ++input;
    if (!*input) return false;
    action = input;
    while (*input && !is_space(*input)) ++input;
    if (*input) *input++ = 0;
    while (is_space(*input)) ++input;
    remainder = input;
    lower_in_place(action);
    return true;
}

void command(char* input) {
    char* action = nullptr; char* arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) { console::line("Usage: pkg status|list|verify|install|info|rollback|remove|run|catalog"); return; }
    if (string_equal(action, "status")) status();
    else if (string_equal(action, "list")) list();
    else if (string_equal(action, "verify")) { if (!*arguments) console::warning("Usage: pkg verify <package.zpk>"); else (void)verify(arguments); }
    else if (string_equal(action, "install")) { if (!*arguments) console::warning("Usage: pkg install <package.zpk>"); else (void)install(arguments); }
    else if (string_equal(action, "info")) { lower_in_place(arguments); if (!*arguments) console::warning("Usage: pkg info <name>"); else info(arguments); }
    else if (string_equal(action, "rollback")) { lower_in_place(arguments); if (!*arguments) console::warning("Usage: pkg rollback <name>"); else (void)rollback(arguments); }
    else if (string_equal(action, "remove")) { lower_in_place(arguments); if (!*arguments) console::warning("Usage: pkg remove <name>"); else (void)remove_package(arguments); }
    else if (string_equal(action, "run")) (void)run_package(arguments);
    else if (string_equal(action, "catalog")) {
        char* sub = nullptr; char* path = nullptr;
        if (!split_subcommand(arguments, sub, path)) package_catalog::print_status();
        else if (string_equal(sub, "status")) package_catalog::print_status();
        else if (string_equal(sub, "update")) { if (!*path) console::warning("Usage: pkg catalog update <signed.zpc>"); else (void)update_catalog(path); }
        else console::warning("Usage: pkg catalog status|update");
    } else console::warning("Unknown pkg action.");
}

bool command_token_equal(const char* line, const char* expected) {
    if (!line) return false;
    while (is_space(*line)) ++line;
    while (*expected && *line && !is_space(*line)) {
        if (ascii_lower(*line) != *expected) return false;
        ++line; ++expected;
    }
    return !*expected && (!*line || is_space(*line));
}

bool dispatch_line(char* line) {
    if (!command_token_equal(line, "pkg")) return false;
    while (is_space(*line)) ++line;
    while (*line && !is_space(*line)) ++line;
    while (is_space(*line)) ++line;
    command(line); return true;
}
