void command_guard(char* args) {
    while (is_space(*args)) ++args;
    char* subcommand = args;
    while (*args && !is_space(*args)) ++args;
    if (*args) { *args++ = 0; while (is_space(*args)) ++args; }
    lower_in_place(subcommand);

    if (!*subcommand || string_equal(subcommand, "status")) {
        begin_view("ZENOVGUARD STATUS");
        security_guard::print_status();
        security_audit::print_status();
        zgdb::print_status();
        return;
    }
    if (string_equal(subcommand, "database")) {
        begin_view("ZENOVGUARD SIGNED DATABASE");
        zgdb::print_status();
        return;
    }
    if (string_equal(subcommand, "update")) {
        begin_view("ZENOVGUARD POLICY UPDATE");
        if (!*args) { console::warning("Usage: guard update <signed-zgdb-path>"); return; }
        if (zgdb::update(args)) console::success("ZGDB_ATOMIC_UPDATE_OK");
        else console::warning("Signed policy update rejected; inspect the serial reason.");
        return;
    }
    if (string_equal(subcommand, "selftest")) {
        begin_view("ZENOVGUARD SELF-TEST");
        if (security_guard::self_test() && security_audit::self_test()) {
            console::success("ZENOV_GUARD_SELFTEST_OK"); serial::line("ZENOV_GUARD_SELFTEST_OK");
        } else {
            console::warning("ZENOV_GUARD_SELFTEST_FAILED"); serial::line("ZENOV_GUARD_SELFTEST_FAILED");
        }
        return;
    }
    if (string_equal(subcommand, "log")) {
        begin_view("ZENOVGUARD PERSISTENT AUDIT");
        lower_in_place(args);
        if (string_equal(args, "verify")) {
            if (security_audit::verify_active()) console::success("ZENOV_GUARD_AUDIT_VERIFY_OK");
            else console::warning("Persistent audit verification failed; execution is locked.");
        } else {
            console::line("Persistent journal:");
            security_audit::print();
            console::line("Session mirror:");
            security_guard::print_audit();
        }
        return;
    }
    if (string_equal(subcommand, "scan")) {
        begin_view("ZENOVGUARD SCAN");
        if (!*args || string_equal(args, "all")) { security_guard::scan_all(); return; }
        security_guard::ScanResult result{};
        if (!security_guard::scan_path(args, result)) { console::warning("Scan failed: file missing, unreadable, checksum-invalid or audit append failed."); return; }
        security_guard::print_result(result);
        if (result.verdict == security_guard::Verdict::infected) security_guard::serial_decision("ZENOV_GUARD_DETECTED", result);
        else security_guard::serial_decision("ZENOV_GUARD_SCAN_RESULT", result);
        return;
    }
    if (string_equal(subcommand, "quarantine")) {
        begin_view("ZENOVGUARD QUARANTINE");
        if (!*args) { console::warning("Usage: guard quarantine <path>"); return; }
        if (security_guard::quarantine_path(args)) console::success("ZENOV_GUARD_QUARANTINE_OK");
        else console::warning("Quarantine rejected or persistent audit commit failed.");
        return;
    }
    console::warning("Usage: guard status | database | update <zgdb> | scan <path|all> | quarantine <path> | log [verify] | selftest");
}
