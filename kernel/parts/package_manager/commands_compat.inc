bool compat_split_subcommand(char* input, char*& action, char*& remainder) {
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

bool compat_check(const char* path) {
    if (!path || !*path) return false;
    storage::FileInfo info{};
    if (!storage::query(path, info) || info.type != storage::zfs_type_file ||
        !info.size || info.size > maximum_package_bytes) {
        console::warning("Compatibility preflight requires a 1..65536 byte file.");
        serial::line("ZENPKG_COMPAT_PREFLIGHT_REJECT reason=file-window");
        return false;
    }
    uint32_t actual = 0U;
    if (!storage::read_file(path, package_storage, maximum_package_bytes, actual) || actual != info.size) {
        console::warning("Compatibility preflight encountered a short read.");
        serial::line("ZENPKG_COMPAT_PREFLIGHT_REJECT reason=io");
        return false;
    }
    const package_compatibility::Result result = package_compatibility::check(
        package_storage, actual, path);
    console::write("  Platform      "); console::line(result.platform);
    console::write("  Format        "); console::line(foreign_format_text(result.format));
    console::write("  Recognized    "); console::line(result.recognized ? "yes" : "no");
    console::write("  Structure     "); console::line(result.structurally_valid ? "valid" : "rejected");
    console::write("  Trust         "); console::line(result.trust_verified ? "verified" : "unverified");
    console::write("  Runtime       "); console::line(result.runtime_ready ? "available" : "unavailable");
    console::write("  Verdict       "); console::line(package_compatibility::verdict_text(result.verdict));
    console::write("  Bytes         "); console::unsigned_dec(actual); console::put('\n');
    console::write("  Reason        "); console::line(result.reason);
    serial::write(result.structurally_valid
        ? "ZENPKG_COMPAT_PREFLIGHT_OK format="
        : "ZENPKG_COMPAT_PREFLIGHT_REJECT format=");
    serial::write(foreign_format_text(result.format));
    serial::write(" structural="); serial::write(result.structurally_valid ? "1" : "0");
    serial::write(" runtime="); serial::write(result.runtime_ready ? "1" : "0");
    serial::write(" trust="); serial::write(result.trust_verified ? "1" : "0");
    serial::write(" verdict="); serial::line(package_compatibility::verdict_text(result.verdict));
    return result.structurally_valid;
}

void compat_command(char* input) {
    char* action = nullptr;
    char* arguments = nullptr;
    if (!compat_split_subcommand(input, action, arguments)) {
        console::line("Usage: pkg compat status|check <file>|run-linux <file> [args]|run-psx <file>");
        return;
    }
    if (string_equal(action, "status")) {
        console::line("  FORMAT        EXACT PREFLIGHT                      EXECUTION");
        console::line("  Linux/i386    static/segments/entry/W^X/ranges     ring-3 minimal");
        console::line("  Windows PE32  headers/sections/entry/W^X/i386      inspect only");
        console::line("  PS-X EXE      payload/load/entry/fill/stack/RAM     R3000A diagnostic");
        console::line("  PS2 R5900     static/segments/entry/W^X/ranges     inspect only");
        console::line("  Xbox XBE      headers/sections/image/W^X/ranges    inspect only");
        console::line("  Linux ABI     write(fd 1/2), exit, exit_group; all else denied");
        console::line("  PS1 ABI       R3000A integer core, delay slots, bounded A0/B0 console HLE");
        console::line("  Trust         raw foreign signatures remain unverified");
        serial::line("ZENPKG_COMPAT_READY validators=5 runtimes=2 fail-closed=1 antivirus-unchanged=1");
    } else if (string_equal(action, "check")) {
        if (!*arguments) console::warning("Usage: pkg compat check <file>");
        else if (!compat_check(arguments)) console::warning("Compatibility preflight rejected the file.");
    } else if (string_equal(action, "run-linux")) {
        if (!*arguments) console::warning("Usage: pkg compat run-linux <file> [arguments]");
        else if (!process::run_linux_i386(arguments)) console::warning("Linux/i386 launch rejected.");
    } else if (string_equal(action, "run-psx")) {
        if (!*arguments) console::warning("Usage: pkg compat run-psx <file>");
        else if (!process::run_psx(arguments)) console::warning("PS1 R3000A launch rejected.");
    } else {
        console::warning("Unknown pkg compat action.");
    }
}
