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
    serial::write("ZENPKG_INFO name="); serial::write(record.name); serial::write(" version="); serial::line(record.active.version);
}

void status() {
    console::write("  State         "); console::line(initialized ? "ready" : "unavailable");
    console::write("  Format        "); console::line("ZENPKG1 / ZPKDB1");
    console::write("  Installed     "); console::unsigned_dec(database.count); console::put('\n');
    console::write("  Generation    "); console::unsigned_dec(database.generation); console::put('\n');
    console::write("  Catalog       "); console::unsigned_dec(authorized_package_count()); console::line(" build-pinned packages");
    console::line("  Trust         exact metadata + package/payload SHA-256 + ZenovGuard/ZGDB precheck");
    console::line("  Transaction   immutable payload + atomic active-reference commit");
    console::line("  Downgrade     blocked; explicit rollback only to retained previous version");
    console::line("  Network       disabled until signed repository role metadata exists");
    serial::line("ZENPKG_STATUS_OK");
}

bool verify(const char* path) {
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(path, package_buffer, storage::file_capacity(), package, package_size);
    if (error != native_package::Error::none) {
        console::write("Package verification failed: "); console::line(native_package::error_text(error));
        serial::line("ZENPKG_VERIFY_REJECTED");
        return false;
    }
    const bool authorized = authorized_package(package);
    console::write("  Name          "); console::line(package.manifest.name);
    console::write("  Version       "); console::line(package.manifest.version);
    console::write("  Entrypoint    "); console::line(package.manifest.entrypoint);
    console::write("  Package bytes "); console::unsigned_dec(package_size); console::put('\n');
    console::write("  Payload bytes "); console::unsigned_dec(package.payload_size); console::put('\n');
    console::write("  Authorization "); console::line(authorized ? "build-pinned" : "not authorized for installation");
    console::success("PKG_VERIFY_OK");
    serial::line(authorized ? "ZENPKG_VERIFY_AUTHORIZED" : "ZENPKG_VERIFY_UNAUTHORIZED");
    serial::line("ZENPKG_VERIFY_OK");
    return true;
}

bool same_storage_path(const char* left, const char* right) {
    char left_normalized[48]{}, right_normalized[48]{};
    return left && right && storage::normalize_path(left, left_normalized) && storage::normalize_path(right, right_normalized) &&
        string_equal(left_normalized, right_normalized);
}

bool allow_execution(const char* path, const uint8_t* data, uint32_t size) {
    if (!initialized || !path || !data || !size) return false;
    security_guard::ScanResult scan = security_guard::scan_memory(path, data, size, false);
    if (scan.verdict != security_guard::Verdict::clean) return false;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = database.records[i];
        if (!record.used || !record.active.valid || !same_storage_path(path, record.active.entrypoint)) continue;
        if (!authorized_reference(record.name, record.active) ||
            !security_guard::digest_equal(scan.digest, record.active.payload_digest)) return false;
        scan.verdict = security_guard::Verdict::trusted;
        storage::path_copy(scan.signature, "ZenPkg.BootstrapCatalog", sizeof(scan.signature));
        if (!security_guard::record(security_guard::AuditAction::execute, scan)) return false;
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
    char saved = *remainder;
    if (saved) *remainder++ = 0;
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

bool split_subcommand(char* input, char*& command, char*& remainder) {
    if (!input) return false;
    while (is_space(*input)) ++input;
    if (!*input) return false;
    command = input;
    while (*input && !is_space(*input)) ++input;
    if (*input) *input++ = 0;
    while (is_space(*input)) ++input;
    remainder = input;
    lower_in_place(command);
    return true;
}

void command(char* input);

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
    command(line);
    return true;
}

void command(char* input) {
    char* action = nullptr;
    char* arguments = nullptr;
    if (!split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg status|list|verify|install|info|rollback|remove|run");
        return;
    }
    if (string_equal(action, "status")) status();
    else if (string_equal(action, "list")) list();
    else if (string_equal(action, "verify")) { if (!*arguments) console::warning("Usage: pkg verify <package.zpk>"); else verify(arguments); }
    else if (string_equal(action, "install")) { if (!*arguments) console::warning("Usage: pkg install <package.zpk>"); else install(arguments); }
    else if (string_equal(action, "info")) { lower_in_place(arguments); if (!*arguments) console::warning("Usage: pkg info <name>"); else info(arguments); }
    else if (string_equal(action, "rollback")) { lower_in_place(arguments); if (!*arguments) console::warning("Usage: pkg rollback <name>"); else rollback(arguments); }
    else if (string_equal(action, "remove")) { lower_in_place(arguments); if (!*arguments) console::warning("Usage: pkg remove <name>"); else remove_package(arguments); }
    else if (string_equal(action, "run")) run_package(arguments);
    else console::warning("Unknown pkg action.");
}
