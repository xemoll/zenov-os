#include "psx_exe.inc"
#include "psx_r3000a.inc"
#include "psx_guest_memory.inc"
namespace process {

constexpr uint32_t psx_instruction_budget = 1000000U;

bool psx_path_allowed(const char* normalized) {
    return normalized && !security_policy::protected_path(normalized) &&
        (string_starts_with(normalized, "/samples/") || string_starts_with(normalized, "/downloads/"));
}

void psx_console_output(void*, char value) { console::put(value); }

bool run_psx(char* input) {
    if (!initialized || !storage::ready() || !input) return false;
    while (is_space(*input)) ++input;
    if (!*input) return false;
    char* end = input;
    while (*end && !is_space(*end)) ++end;
    if (*end) *end = 0;

    char path[48]{};
    if (!storage::normalize_path(input, path) || !psx_path_allowed(path)) {
        console::warning("PS1 sandbox accepts only /samples or /downloads files.");
        serial::line("PSX_R3000A_REJECT reason=path-policy");
        return false;
    }
    storage::FileInfo info{};
    if (!storage::query(path, info) || info.type != storage::zfs_type_file ||
        !info.size || info.size > application_buffer_bytes) {
        serial::line("PSX_R3000A_REJECT reason=file");
        return false;
    }
    uint32_t file_size = 0U;
    if (!storage::guarded_read_file(path, application_buffer, application_buffer_bytes, file_size) ||
        file_size != info.size) {
        serial::line("PSX_R3000A_REJECT reason=guarded-read");
        return false;
    }
    const package_manager::package_compatibility::Result preflight =
        package_manager::package_compatibility::check(application_buffer, file_size, path);
    if (preflight.format != package_manager::package_foreign::Format::playstation_psx_exe ||
        preflight.verdict != package_manager::package_compatibility::Verdict::runnable_sandbox) {
        serial::line("PSX_R3000A_REJECT reason=zenpkg-preflight");
        return false;
    }
    psx_exe::Layout layout{};
    if (!psx_exe::validate(application_buffer, file_size, layout)) {
        serial::line("PSX_R3000A_REJECT reason=loader");
        return false;
    }

    uint8_t* ram = psx_guest_memory::acquire();
    if (!ram) {
        serial::line("PSX_R3000A_REJECT reason=guest-memory");
        return false;
    }
    uint32_t load_offset = 0U;
    bool loaded = psx_exe::address_offset(layout.load_address, load_offset);
    if (loaded) memcpy(ram + load_offset, application_buffer + psx_exe::header_bytes, layout.payload_bytes);
    if (loaded && layout.fill_bytes) {
        uint32_t fill_offset = 0U;
        loaded = psx_exe::address_offset(layout.fill_address, fill_offset);
        if (loaded) memset(ram + fill_offset, 0, layout.fill_bytes);
    }
    if (!loaded) {
        (void)psx_guest_memory::release();
        serial::line("PSX_R3000A_REJECT reason=load-range");
        return false;
    }

    psx_r3000a::Machine machine{};
    psx_r3000a::initialize(machine, ram, psx_guest_memory::bytes, layout.entry,
                           layout.stack_pointer, layout.global_pointer, psx_console_output, nullptr);
    storage::path_copy(current_application, path, sizeof(current_application));
    string_copy(current_format, "PSX", sizeof(current_format));
    console::write("Launching "); console::write(path); console::line(" [PS1 R3000A diagnostic sandbox]");
    serial::write("PSX_R3000A_START path="); serial::write(path);
    serial::write(" entry="); serial_hex(layout.entry); serial::write("\r\n");

    const psx_r3000a::StopReason stop = psx_r3000a::run(machine, psx_instruction_budget);
    const bool memory_released = psx_guest_memory::release();
    serial::write("PSX_R3000A_STOP reason="); serial::write(psx_r3000a::stop_name(stop));
    serial::write(" steps="); serial_u32(machine.steps);
    serial::write(" output="); serial_u32(machine.output_bytes);
    serial::write(" memory-released="); serial::put(memory_released ? '1' : '0'); serial::write("\r\n");
    if (stop == psx_r3000a::StopReason::exited && memory_released) {
        console::write("PS1 diagnostic exited with code "); console::unsigned_dec(machine.exit_code); console::put('\n');
        serial::write("PSX_R3000A_EXIT code="); serial_u32(machine.exit_code); serial::write("\r\n");
        serial::line("PSX_GUEST_RAM_WIPED pages=512 bytes=2097152");
        return true;
    }
    console::warning("PS1 diagnostic sandbox stopped fail-closed.");
    serial::write("PSX_R3000A_REJECT reason="); serial::line(psx_r3000a::stop_name(stop));
    return false;
}

} // namespace process
