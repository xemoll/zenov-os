void command_guard(char* args) {
    while (is_space(*args)) ++args;
    char* subcommand = args;
    while (*args && !is_space(*args)) ++args;
    if (*args) { *args++ = 0; while (is_space(*args)) ++args; }
    lower_in_place(subcommand);

    if (!*subcommand || string_equal(subcommand, "status")) {
        begin_view("ZENOVGUARD STATUS");
        security_guard::print_status();
        process::print_capability_status();
        zcap::print_status();
        zmid::print_status();
        zrwp::print_status();
        security_audit::print_status();
        zgdb::print_status();
        return;
    }
    if (string_equal(subcommand, "capabilities")) {
        begin_view("APPLICATION SYSCALL CAPABILITIES");
        process::print_capability_profiles();
        return;
    }
    if (string_equal(subcommand, "capability-policy")) {
        begin_view("SIGNED SYSCALL CAPABILITY POLICY");
        zcap::print_status();
        return;
    }
    if (string_equal(subcommand, "capability-update")) {
        begin_view("SYSCALL CAPABILITY POLICY UPDATE");
        if (!*args) { console::warning("Usage: guard capability-update <signed-zcap-path>"); return; }
        if (zcap::update(args)) console::success("ZCAP_ATOMIC_UPDATE_OK");
        else console::warning("Signed capability policy update rejected; inspect the serial reason.");
        return;
    }
    if (string_equal(subcommand, "intelligence")) {
        begin_view("ZENOVGUARD SIGNED MALWARE INTELLIGENCE");
        zmid::print_status();
        return;
    }
    if (string_equal(subcommand, "intelligence-update")) {
        begin_view("MALWARE INTELLIGENCE UPDATE");
        if (!*args) { console::warning("Usage: guard intelligence-update <signed-zmid-path>"); return; }
        if (zmid::update(args)) console::success("ZMID_ATOMIC_UPDATE_OK");
        else console::warning("Signed intelligence update rejected; inspect the serial reason.");
        return;
    }
    if (string_equal(subcommand, "ransomware")) {
        begin_view("SIGNED RANSOMWARE DEFENSE POLICY");
        zrwp::print_status();
        return;
    }
    if (string_equal(subcommand, "ransomware-update")) {
        begin_view("RANSOMWARE DEFENSE POLICY UPDATE");
        if (!*args) { console::warning("Usage: guard ransomware-update <signed-zrwp-path>"); return; }
        if (zrwp::update(args)) console::success("ZRWP_ATOMIC_UPDATE_OK");
        else console::warning("Signed ransomware policy update rejected; inspect the serial reason.");
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
        if (security_guard::self_test() && security_audit::self_test() && zcap::self_test() && zmid::self_test() && zrwp::self_test() && process::capability_self_test()) {
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
        if (!*args || string_equal(args, "list")) { security_guard::print_quarantine(); return; }
        if (string_starts_with(args, "delete ")) {
            char* path = args + 7U;
            while (is_space(*path)) ++path;
            if (!*path) { console::warning("Usage: guard quarantine delete <quarantine-path>"); return; }
            if (security_guard::delete_quarantine(path)) console::success("ZENOV_GUARD_QUARANTINE_DELETE_OK");
            else console::warning("Quarantine deletion rejected or audit append failed.");
            return;
        }
        if (security_guard::quarantine_path(args)) console::success("ZENOV_GUARD_QUARANTINE_OK");
        else console::warning("Quarantine rejected or persistent audit commit failed.");
        return;
    }
    console::warning("Usage: guard status | intelligence | intelligence-update <zmid> | ransomware | ransomware-update <zrwp> | capabilities | capability-policy | capability-update <zcap> | database | update <zgdb> | scan <path|all> | quarantine [list|delete <path>|<path>] | log [verify] | selftest");
}
