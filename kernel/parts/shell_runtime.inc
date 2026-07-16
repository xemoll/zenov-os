namespace {
void shell_run() {
    if constexpr (zenov_generated::kShellLineCapacity == 80) {
        shell_run_legacy_80();
        return;
    }
    char line[zenov_generated::kShellLineCapacity];
    for (;;) {
        console::prompt(zenov_generated::kPrompt);
        read_line(line, sizeof(line));
        execute(line);
    }
}
}
