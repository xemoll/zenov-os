namespace {
bool dispatch_security_command(const char* input) {
    if (!input) return false;
    char line[zenov_generated::kShellLineCapacity];
    uint32_t length = 0;
    while (input[length]) {
        if (length + 1U >= sizeof(line)) return false;
        line[length] = input[length]; ++length;
    }
    line[length] = 0;
    char* command = line;
    while (is_space(*command)) ++command;
    char* args = command;
    while (*args && !is_space(*args)) ++args;
    if (*args) { *args++ = 0; while (is_space(*args)) ++args; }
    lower_in_place(command);
    if (!string_equal(command, "guard") && !string_equal(command, "antivirus")) return false;
    command_guard(args); return true;
}

void dispatch_shell_line(char* line) {
    if (package_manager::dispatch_line(line)) return;
    if (dispatch_security_command(line)) return;
    execute(line);
}

void shell_run() {
    if constexpr (zenov_generated::kShellLineCapacity == 80) {
        auto* legacy_reference = &shell_run_legacy_80;
        (void)legacy_reference;
        char line[80];
        for (;;) {
            console::prompt(zenov_generated::kPrompt);
            read_line(line, sizeof(line));
            dispatch_shell_line(line);
        }
    }
    char line[zenov_generated::kShellLineCapacity];
    for (;;) {
        console::prompt(zenov_generated::kPrompt);
        read_line(line, sizeof(line));
        dispatch_shell_line(line);
    }
}
} // namespace
