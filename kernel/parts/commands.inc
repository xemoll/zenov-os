uint32_t usable_memory_kib() {
    if (boot_info()->magic != boot_magic) return 0;
    uint32_t usable = 0;
    for (uint32_t i = 0; i < boot_info()->e820_count; ++i) {
        const auto& entry = e820_entries()[i];
        if (entry.type == 1) usable += entry.length_high ? 0x3FFFFFU : (entry.length_low >> 10);
    }
    return usable;
}

namespace console {
void show_home() {
    clear();
    draw_panel(1, 4, 38, 8, " SYSTEM ");
    write_at(5, 3, "State", palette.muted, palette.body_bg);
    write_at(5, 16, "READY", palette.good, palette.body_bg);
    write_at(6, 3, "Architecture", palette.muted, palette.body_bg);
    write_at(6, 16, "i686", palette.body_fg, palette.body_bg);
    write_at(7, 3, "Mode", palette.muted, palette.body_bg);
    write_at(7, 16, "32-bit protected", palette.body_fg, palette.body_bg);
    write_at(8, 3, "Memory", palette.muted, palette.body_bg);
    write_uint_at(8, 16, usable_memory_kib(), palette.body_fg, palette.body_bg);
    write_at(8, 23, "KiB usable", palette.body_fg, palette.body_bg);
    write_at(9, 3, "Storage", palette.muted, palette.body_bg);
    write_at(9, 16, "FAT12 / read-only", palette.body_fg, palette.body_bg);
    write_at(10, 3, "Input", palette.muted, palette.body_bg);
    write_at(10, 16, "PS/2 keyboard", palette.body_fg, palette.body_bg);

    draw_panel(41, 4, 38, 8, " QUICK ACCESS ");
    write_at(5, 43, "F1", palette.accent, palette.body_bg); write_at(5, 48, "Command reference", palette.body_fg, palette.body_bg);
    write_at(6, 43, "F2", palette.accent, palette.body_bg); write_at(6, 48, "System details", palette.body_fg, palette.body_bg);
    write_at(7, 43, "F3", palette.accent, palette.body_bg); write_at(7, 48, "Browse files", palette.body_fg, palette.body_bg);
    write_at(8, 43, "F4", palette.accent, palette.body_bg); write_at(8, 48, "Return home", palette.body_fg, palette.body_bg);
    write_at(9, 43, "Tab", palette.accent, palette.body_bg); write_at(9, 48, "Complete a command", palette.body_fg, palette.body_bg);
    write_at(10, 43, "Esc", palette.accent, palette.body_bg); write_at(10, 48, "Clear current input", palette.body_fg, palette.body_bg);

    draw_panel(1, 13, 78, 8, " WORKSPACE ");
    write_at(14, 3, "Start with", palette.muted, palette.body_bg);
    write_at(14, 16, "status", palette.accent, palette.body_bg);
    write_at(14, 25, "devices", palette.accent, palette.body_bg);
    write_at(14, 35, "files", palette.accent, palette.body_bg);
    write_at(14, 43, "cat WELCOME", palette.accent, palette.body_bg);
    write_at(16, 3, "Editing", palette.muted, palette.body_bg);
    write_at(16, 16, "Left/Right, Home/End and Delete are available.", palette.body_fg, palette.body_bg);
    write_at(17, 3, "History", palette.muted, palette.body_bg);
    write_at(17, 16, "Use Up/Down to recall the last 16 commands.", palette.body_fg, palette.body_bg);
    write_at(18, 3, "Themes", palette.muted, palette.body_bg);
    write_at(18, 16, "theme midnight | graphite | amber", palette.body_fg, palette.body_bg);
    move_to(22, 0);
}
}

void begin_view(const char* title) {
    console::clear_content();
    console::title(title);
    console::line("");
}
void command_help() {
    begin_view("COMMAND REFERENCE");
    console::line("  Navigation   home  help  clear");
    console::line("  System       info  status  devices  disk  cpu  mem  memmap");
    console::line("  Session      uptime  date  time  history  bootlog");
    console::line("  Files        ls  files  cat <name>  open <name>");
    console::line("  Tools        echo  calc  theme");
    console::line("  Power        reboot  halt");
    if (zenov_generated::kStaticCommandCount) {
        console::write("  Project      ");
        for (uint32_t i = 0; i < zenov_generated::kStaticCommandCount; ++i) {
            if (i) console::write("  ");
            console::write(zenov_generated::kStaticCommands[i].name);
        }
        console::put('\n');
    }
    console::line("");
    console::line("  F1-F4 open common views. Tab completes commands. Esc clears input.");
}
void command_info() {
    begin_view("SYSTEM DETAILS");
    console::write("  Product       "); console::write(zenov_generated::kSystemName); console::put(' '); console::line(zenov_generated::kVersion);
    console::line("  Target        i686-zenov-none");
    console::line("  Execution     32-bit protected mode");
    console::line("  Boot          legacy BIOS / FAT12");
    console::write("  Boot drive    "); console::hex(boot_info()->drive, 2); console::put('\n');
    console::write("  Kernel file   "); console::unsigned_dec(static_cast<uint32_t>(&__kernel_file_end - &__kernel_start)); console::line(" bytes");
    console::write("  Runtime       "); console::unsigned_dec(static_cast<uint32_t>(&__kernel_end - &__kernel_start)); console::line(" bytes");
    console::line("  Source stack  Zenov, assembler, freestanding C++17");
}
void command_status() {
    begin_view("SYSTEM STATUS");
    console::success("  READY         Core services are running.");
    console::line("  Interrupts    IDT active; PIC remapped");
    console::line("  Timer         PIT channel 0 at 100 Hz");
    console::line("  Keyboard      PS/2 controller on IRQ1");
    console::line("  Console       VGA text mode with COM1 mirror");
    console::line("  Storage       FAT12 boot volume, read-only");
    console::write("  Uptime        "); console::unsigned_dec(timer_ticks / 100U); console::line(" seconds");
}
void command_devices() {
    begin_view("DEVICES");
    console::line("  VGA0          80x25 color text console");
    console::line("  COM1          0x03F8 serial diagnostics");
    console::line("  KBD0          PS/2 keyboard, IRQ1");
    console::line("  PIT0          100 Hz system timer, IRQ0");
    console::line("  RTC0          CMOS real-time clock");
    console::line("  FDC0          BIOS boot image interface");
}
void command_disk() {
    begin_view("BOOT VOLUME");
    console::line("  Label         ZENOVOS");
    console::line("  Filesystem    FAT12");
    console::line("  Capacity      1,440 KiB");
    console::line("  Boot method   legacy BIOS floppy image");
    console::line("  Access        read-only after startup");
    console::write("  Kernel        "); console::unsigned_dec(static_cast<uint32_t>(&__kernel_file_end - &__kernel_start)); console::line(" bytes");
}
void command_cpu() {
    begin_view("PROCESSOR");
    if (!cpuid_available()) { console::warning("  CPUID is unavailable on this processor."); return; }
    uint32_t a, b, c, d; cpuid(0, a, b, c, d);
    char vendor[13]; memcpy(vendor, &b, 4); memcpy(vendor + 4, &d, 4); memcpy(vendor + 8, &c, 4); vendor[12] = 0;
    console::write("  Vendor        "); console::line(vendor);
    console::write("  Maximum leaf  "); console::unsigned_dec(a); console::put('\n');
    cpuid(1, a, b, c, d);
    console::write("  Signature     "); console::hex(a); console::put('\n');
    console::write("  Features EDX  "); console::hex(d); console::put('\n');
    console::write("  Features ECX  "); console::hex(c); console::put('\n');
}
void command_mem() {
    begin_view("MEMORY");
    if (boot_info()->magic != boot_magic) { console::warning("  Memory information is unavailable."); return; }
    console::write("  E820 regions  "); console::unsigned_dec(boot_info()->e820_count); console::put('\n');
    console::write("  Usable        "); console::unsigned_dec(usable_memory_kib()); console::line(" KiB (approx.)");
    console::line("  Discovery     BIOS E820 map captured before protected mode");
}
void command_memmap() {
    begin_view("MEMORY MAP");
    for (uint32_t i = 0; i < boot_info()->e820_count; ++i) {
        const auto& entry = e820_entries()[i];
        console::write("  "); console::unsigned_dec(i); console::write("  base "); console::hex(entry.base_low);
        console::write("  size "); console::hex(entry.length_low); console::write("  type "); console::unsigned_dec(entry.type); console::put('\n');
    }
}
void command_ls() {
    begin_view("FILES");
    console::line("  NAME            TYPE       SIZE");
    console::line("  --------------- ---------- --------");
    for (uint32_t i = 0; i < zenov_generated::kFileCount; ++i) {
        console::write("  "); console::write(zenov_generated::kFiles[i].name);
        const uint32_t name_length = static_cast<uint32_t>(string_length(zenov_generated::kFiles[i].name));
        for (uint32_t p = name_length; p < 16; ++p) console::put(' ');
        console::write("text       ");
        console::unsigned_dec(static_cast<uint32_t>(string_length(zenov_generated::kFiles[i].text))); console::line(" bytes");
    }
}
void command_cat(const char* name) {
    while (is_space(*name)) ++name;
    for (uint32_t i = 0; i < zenov_generated::kFileCount; ++i) {
        if (!string_equal(name, zenov_generated::kFiles[i].name)) continue;
        begin_view(zenov_generated::kFiles[i].name);
        console::write(zenov_generated::kFiles[i].text);
        return;
    }
    console::warning("File not found. Use files to list available names.");
}
void command_history() {
    begin_view("COMMAND HISTORY");
    const uint32_t available = history_count < 16 ? history_count : 16;
    const uint32_t oldest = history_count - available;
    if (available == 0) { console::line("  No commands have been entered in this session."); return; }
    for (uint32_t i = oldest; i < history_count; ++i) {
        console::write("  "); console::unsigned_dec(i + 1); console::write("  "); console::line(history[i % 16]);
    }
}
void command_calc(const char* args) {
    int32_t left, right;
    if (!parse_i32(args, left)) { console::warning("Usage: calc <integer> <+|-|*|/|%> <integer>"); return; }
    while (is_space(*args)) ++args;
    const char operation = *args++;
    if (!parse_i32(args, right)) { console::warning("The right operand is invalid."); return; }
    int32_t result = 0;
    switch (operation) {
        case '+': result = left + right; break;
        case '-': result = left - right; break;
        case '*': result = left * right; break;
        case '/': if (right == 0) { console::warning("Division by zero is not allowed."); return; } result = left / right; break;
        case '%': if (right == 0) { console::warning("Modulo by zero is not allowed."); return; } result = left % right; break;
        default: console::warning("Unsupported operator."); return;
    }
    console::write("Result: "); console::signed_dec(result); console::put('\n');
}
void command_theme(char* args) {
    while (is_space(*args)) ++args;
    lower_in_place(args);
    if (!console::select_theme(args)) {
        console::warning("Usage: theme midnight | graphite | amber");
        return;
    }
    console::show_home();
}
void command_color(const char* args) {
    int32_t value;
    if (!parse_i32(args, value) || value < 0 || value > 15) { console::warning("Usage: color <0..15>"); return; }
    console::palette.body_fg = static_cast<uint8_t>(value);
    console::show_home();
}
void command_rtc(bool date) {
    const Rtc value = rtc_read();
    if (date) { two_digits(value.day); console::put('-'); two_digits(value.month); console::write("-20"); two_digits(value.year); }
    else { two_digits(value.hour); console::put(':'); two_digits(value.minute); console::put(':'); two_digits(value.second); }
    console::put('\n');
}
void command_bootlog() {
    begin_view("BOOT LOG");
    for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) {
        console::write("  "); console::line(zenov_generated::kBootMessages[i]);
    }
}
[[noreturn]] void reboot() {
    console::warning("Restarting system...");
    disable_interrupts();
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
        if (string_equal(command, zenov_generated::kStaticCommands[i].name)) {
            begin_view(zenov_generated::kStaticCommands[i].name);
            console::line(zenov_generated::kStaticCommands[i].text);
            return;
        }
    }
    if (string_equal(command, "help")) command_help();
    else if (string_equal(command, "home")) console::show_home();
    else if (string_equal(command, "info") || string_equal(command, "system")) command_info();
    else if (string_equal(command, "status")) command_status();
    else if (string_equal(command, "devices")) command_devices();
    else if (string_equal(command, "disk")) command_disk();
    else if (string_equal(command, "ver") || string_equal(command, "version")) console::line(zenov_generated::kVersion);
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
    else if (string_equal(command, "theme")) command_theme(args);
    else if (string_equal(command, "color")) command_color(args);
    else if (string_equal(command, "ls") || string_equal(command, "files") || string_equal(command, "dir")) command_ls();
    else if (string_equal(command, "cat") || string_equal(command, "open") || string_equal(command, "view")) command_cat(args);
    else if (string_equal(command, "history")) command_history();
    else if (string_equal(command, "bootlog")) command_bootlog();
    else if (string_equal(command, "clear") || string_equal(command, "cls")) console::clear_content();
    else if (string_equal(command, "reboot")) reboot();
    else if (string_equal(command, "halt") || string_equal(command, "shutdown")) { console::warning("System halted."); halt_forever(); }
    else if (string_equal(command, "panic")) panic("Manual panic requested from the console.");
    else console::warning("Command not found. Press F1 or type help.");
}

void shell_run() {
    char line[80];
    for (;;) {
        console::prompt(zenov_generated::kPrompt);
        read_line(line, sizeof(line));
        execute(line);
    }
}

} // namespace