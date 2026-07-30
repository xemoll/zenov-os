bool dispatch_tpm_command(char* line) {
    if (!line || !package_manager::command_token_equal(line, "tpm")) return false;
    char* args = line + 3U;
    while (is_space(*args)) ++args;
    char* end = args;
    while (*end && !is_space(*end)) ++end;
    if (*end) *end = 0;
    lower_in_place(args);

    begin_view("TPM 2.0 / HARDWARE FRESHNESS SUBSTRATE");
    if (!*args || string_equal(args, "status")) {
        tpm2::print_status();
        return true;
    }
    if (string_equal(args, "provision")) {
        if (tpm2::provision_counter()) console::success("TPM2_NV_COUNTER_PROVISIONED");
        else console::warning("TPM NV counter provisioning failed; owner authorization may be unavailable or the index is noncanonical.");
        return true;
    }
    if (string_equal(args, "increment")) {
        if (tpm2::increment_counter()) console::success("TPM2_NV_COUNTER_INCREMENTED");
        else console::warning("TPM NV counter increment failed; inspect the serial response code.");
        return true;
    }
    if (string_equal(args, "selftest")) {
        if (tpm2::self_test()) console::success("TPM2_SELFTEST_OK");
        else console::warning("TPM2 self-test failed.");
        return true;
    }
    console::warning("Usage: tpm status | provision | increment | selftest");
    return true;
}
