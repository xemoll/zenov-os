void command_help() {
    console::title("Commands");
    console::line("  System   info  ver  uname  cpu  mem  memmap  uptime  date  time");
    console::line("  Files    ls  cat");
    console::line("  Shell    echo  calc  color  history  bootlog  clear");
    console::line("  Power    reboot  halt");
    console::line("  Debug    ticks  panic");
    if (zenov_generated::kStaticCommandCount) {
        console::write("  Project  ");
        for (uint32_t i = 0; i < zenov_generated::kStaticCommandCount; ++i) {
            if (i) console::write("  ");
            console::write(zenov_generated::kStaticCommands[i].name);
        }
        console::put('\n');
    }
}
void command_info() {
    console::title("System");
    console::write("  Name     "); console::write(zenov_generated::kSystemName); console::put(' '); console::line(zenov_generated::kVersion);
    console::line("  Target   i686-zenov-none");
    console::line("  Mode     protected / 32-bit");
    console::write("  Boot     drive "); console::hex(boot_info()->drive, 2); console::put('\n');
    console::write("  Kernel   "); console::unsigned_dec(static_cast<uint32_t>(&__kernel_file_end - &__kernel_start));
    console::write(" bytes on disk, "); console::unsigned_dec(static_cast<uint32_t>(&__kernel_end - &__kernel_start)); console::line(" bytes loaded");
}
void command_cpu() {
    if (!cpuid_available()) { console::line("CPUID is not available on this processor."); return; }
    uint32_t a, b, c, d; cpuid(0, a, b, c, d);
    char vendor[13]; memcpy(vendor, &b, 4); memcpy(vendor + 4, &d, 4); memcpy(vendor + 8, &c, 4); vendor[12] = 0;
    console::title("Processor");
    console::write("  Vendor   "); console::line(vendor);
    console::write("  Max leaf "); console::unsigned_dec(a); console::put('\n');
    cpuid(1, a, b, c, d);
    console::write("  Signature "); console::hex(a); console::put('\n');
    console::write("  EDX       "); console::hex(d); console::put('\n');
    console::write("  ECX       "); console::hex(c); console::put('\n');
}
void command_mem() {
    if (boot_info()->magic != boot_magic) { console::line("Memory information is unavailable."); return; }
    uint32_t usable_kib = 0;
    for (uint32_t i = 0; i < boot_info()->e820_count; ++i) {
        const auto& entry = e820_entries()[i];
        if (entry.type == 1) usable_kib += entry.length_high ? 0x3FFFFFU : (entry.length_low >> 10);
    }
    console::title("Memory");
    console::write("  Regions  "); console::unsigned_dec(boot_info()->e820_count); console::put('\n');
    console::write("  Usable   "); console::unsigned_dec(usable_kib); console::line(" KiB (approx.)");
}
void command_memmap() {
    console::title("Memory map");
    for (uint32_t i = 0; i < boot_info()->e820_count; ++i) {
        const auto& entry = e820_entries()[i];
        console::write("  "); console::unsigned_dec(i); console::write("  base "); console::hex(entry.base_low);
        console::write("  size "); console::hex(entry.length_low); console::write("  type "); console::unsigned_dec(entry.type); console::put('\n');
    }
}
void command_ls() {
    console::title("Files");
    for (uint32_t i = 0; i < zenov_generated::kFileCount; ++i) {
        console::write("  "); console::write(zenov_generated::kFiles[i].name); console::write("  ");
        console::unsigned_dec(static_cast<uint32_t>(string_length(zenov_generated::kFiles[i].text))); console::line(" bytes");
    }
}
void command_cat(const char* name) {
    while (is_space(*name)) ++name;
    for (uint32_t i = 0; i < zenov_generated::kFileCount; ++i) if (string_equal(name, zenov_generated::kFiles[i].name)) { console::write(zenov_generated::kFiles[i].text); return; }
    console::line("File not found.");
}
void command_history() {
    console::title("History");
    const uint32_t available = history_count < 8 ? history_count : 8;
    const uint32_t oldest = history_count - available;
    for (uint32_t i = oldest; i < history_count; ++i) { console::write("  "); console::unsigned_dec(i + 1); console::write("  "); console::line(history[i % 8]); }
}
void command_calc(const char* args) {
    int32_t left, right;
    if (!parse_i32(args, left)) { console::line("Usage: calc <integer> <+|-|*|/|%> <integer>"); return; }
    while (is_space(*args)) ++args;
    const char operation = *args++;
    if (!parse_i32(args, right)) { console::line("The right operand is invalid."); return; }
    int32_t result = 0;
    switch (operation) {
        case '+': result = left + right; break; case '-': result = left - right; break; case '*': result = left * right; break;
        case '/': if (right == 0) { console::line("Division by zero is not allowed."); return; } result = left / right; break;
        case '%': if (right == 0) { console::line("Modulo by zero is not allowed."); return; } result = left % right; break;
        default: console::line("Unsupported operator."); return;
    }
    console::signed_dec(result); console::put('\n');
}
void command_color(const char* args) {
    int32_t value;
    if (!parse_i32(args, value) || value < 0 || value > 15) { console::line("Usage: color <0..15>"); return; }
    console::set_color(static_cast<uint8_t>(value), zenov_generated::kBackground);
    console::line("Text color updated.");
}
void command_rtc(bool date) {
    const Rtc value = rtc_read();
    if (date) { two_digits(value.day); console::put('-'); two_digits(value.month); console::write("-20"); two_digits(value.year); }
    else { two_digits(value.hour); console::put(':'); two_digits(value.minute); console::put(':'); two_digits(value.second); }
    console::put('\n');
}
[[noreturn]] void reboot() {
    console::line("Restarting..."); disable_interrupts();
    for (uint32_t i = 0; i < 100000 && (in8(0x64) & 0x02); ++i) {}
    out8(0x64, 0xFE);
    struct [[gnu::packed]] { uint16_t limit; uint32_t base; } empty{0, 0};
    asm volatile("lidt %0; int3" : : "m"(empty));
    halt_forever();
}

void execute(char* line) {
    char* command = line;
    while (is_space(*command)) ++command;
    char* args = command;
    while (*args && !is_space(*args)) ++args;
    if (*args) { *args++ = 0; while (is_space(*args)) ++args; }
    lower_in_place(command);
    if (!*command) return;

    for (uint32_t i = 0; i < zenov_generated::kStaticCommandCount; ++i) {
        if (string_equal(command, zenov_generated::kStaticCommands[i].name)) { console::line(zenov_generated::kStaticCommands[i].text); return; }
    }
    if (string_equal(command, "help")) command_help();
    else if (string_equal(command, "info")) command_info();
    else if (string_equal(command, "ver")) console::line(zenov_generated::kVersion);
    else if (string_equal(command, "uname")) console::line("ZenovOS 0.1.0 i686-zenov-none");
    else if (string_equal(command, "cpu")) command_cpu();
    else if (string_equal(command, "mem")) command_mem();
    else if (string_equal(command, "memmap")) command_memmap();
    else if (string_equal(command, "uptime")) { console::unsigned_dec(timer_ticks / 100U); console::line(" seconds"); }
    else if (string_equal(command, "ticks")) { console::unsigned_dec(timer_ticks); console::put('\n'); }
    else if (string_equal(command, "date")) command_rtc(true);
    else if (string_equal(command, "time")) command_rtc(false);
    else if (string_equal(command, "echo")) console::line(args);
    else if (string_equal(command, "calc")) command_calc(args);
    else if (string_equal(command, "color")) command_color(args);
    else if (string_equal(command, "ls")) command_ls();
    else if (string_equal(command, "cat")) command_cat(args);
    else if (string_equal(command, "history")) command_history();
    else if (string_equal(command, "bootlog")) { console::title("Boot log"); for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) { console::write("  "); console::line(zenov_generated::kBootMessages[i]); } }
    else if (string_equal(command, "clear") || string_equal(command, "cls")) console::show_home();
    else if (string_equal(command, "reboot")) reboot();
    else if (string_equal(command, "halt")) { console::line("System halted."); halt_forever(); }
    else if (string_equal(command, "panic")) panic("manual panic requested from shell");
    else console::line("Command not found. Type help.");
}

void shell_run() {
    char line[80];
    for (;;) { console::prompt(zenov_generated::kPrompt); read_line(line, sizeof(line)); execute(line); }
}

} // namespace
