#include "linux_i386_elf.inc"
namespace process {
constexpr uint32_t linux_elf_pt_load = linux_i386_elf::pt_load;

bool linux_i386_path_allowed(const char* normalized) {
    return normalized && !security_policy::protected_path(normalized) &&
        (string_starts_with(normalized, "/samples/") || string_starts_with(normalized, "/downloads/"));
}

bool linux_i386_static_layout(const uint8_t* file, uint32_t file_size, uint32_t& image_bias) {
    linux_i386_elf::Layout layout{};
    if (!linux_i386_elf::validate(file, file_size, stack_base, paging::page_size, layout)) return false;
    image_bias = layout.image_bias;
    return true;
}

bool load_linux_i386(const uint8_t* file, uint32_t file_size, uint32_t& entry, uint32_t& image_bias) {
    if (!linux_i386_static_layout(file, file_size, image_bias)) return false;
    const auto* header = reinterpret_cast<const Elf32Header*>(file);
    paging::clear_user();
    for (uint32_t i = 0U; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const Elf32ProgramHeader*>(
            file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (segment->type != linux_elf_pt_load || !segment->memory_size) continue;
        const uint32_t relative = segment->virtual_address - image_bias;
        if (!paging::map_user(relative, segment->memory_size, true)) return false;
        memcpy(reinterpret_cast<void*>(user_base + relative), file + segment->offset, segment->file_size);
        if (segment->memory_size > segment->file_size)
            memset(reinterpret_cast<void*>(user_base + relative + segment->file_size), 0,
                   segment->memory_size - segment->file_size);
    }
    for (uint32_t i = 0U; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const Elf32ProgramHeader*>(
            file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (segment->type != linux_elf_pt_load || !segment->memory_size) continue;
        if (!paging::protect_user(segment->virtual_address - image_bias, segment->memory_size,
                                  (segment->flags & 2U) != 0U)) return false;
    }
    entry = header->entry - image_bias;
    if (!map_stack() || paging::writable_at(entry)) return false;
    return true;
}

bool build_linux_i386_stack(char* arguments[max_arguments], uint32_t count, uint32_t image_bias,
                            uint32_t& initial_stack) {
    uint32_t offsets[max_arguments]{};
    uint32_t top = stack_top;
    for (uint32_t i = count; i > 0U; --i) {
        const uint32_t length = static_cast<uint32_t>(string_length(arguments[i - 1U])) + 1U;
        if (length > max_argument_bytes || length > top - stack_base) return false;
        top -= length;
        memcpy(reinterpret_cast<void*>(user_base + top), arguments[i - 1U], length);
        offsets[i - 1U] = image_bias + top;
    }
    top &= ~3U;
    const uint32_t words = count + 5U;
    if (words * 4U > top - stack_base) return false;
    top -= words * 4U;
    auto* stack = reinterpret_cast<uint32_t*>(user_base + top);
    uint32_t at = 0U;
    stack[at++] = count;
    for (uint32_t i = 0U; i < count; ++i) stack[at++] = offsets[i];
    stack[at++] = 0U;
    stack[at++] = 0U;
    stack[at++] = 0U;
    stack[at] = 0U;
    initial_stack = image_bias + top;
    return true;
}

bool run_linux_i386(char* input) {
    if (!initialized || !storage::ready() || !input) return false;
    char* arguments[max_arguments]{};
    const uint32_t argument_count = parse_arguments(input, arguments);
    if (!argument_count) return false;

    char path[48]{};
    if (!storage::normalize_path(arguments[0], path) || !linux_i386_path_allowed(path)) {
        console::warning("Linux/i386 sandbox accepts only /samples or /downloads files.");
        serial::line("LINUX_I386_COMPAT_REJECT reason=path-policy");
        return false;
    }
    storage::FileInfo info{};
    if (!storage::query(path, info) || info.type != storage::zfs_type_file ||
        !info.size || info.size > application_buffer_bytes) {
        serial::line("LINUX_I386_COMPAT_REJECT reason=file");
        return false;
    }
    uint32_t file_size = 0U;
    if (!storage::guarded_read_file(path, application_buffer, application_buffer_bytes, file_size) ||
        file_size != info.size) {
        serial::line("LINUX_I386_COMPAT_REJECT reason=guarded-read");
        return false;
    }

    const package_manager::package_compatibility::Result preflight =
        package_manager::package_compatibility::check(application_buffer, file_size, path);
    if (preflight.verdict != package_manager::package_compatibility::Verdict::runnable_sandbox) {
        serial::line("LINUX_I386_COMPAT_REJECT reason=zenpkg-preflight");
        return false;
    }

    uint32_t entry = 0U, image_bias = 0U;
    if (!load_linux_i386(application_buffer, file_size, entry, image_bias)) {
        paging::clear_user();
        serial::line("LINUX_I386_COMPAT_REJECT reason=static-elf-policy");
        return false;
    }
    uint32_t initial_stack = 0U;
    if (!build_linux_i386_stack(arguments, argument_count, image_bias, initial_stack)) {
        paging::clear_user();
        serial::line("LINUX_I386_COMPAT_REJECT reason=stack");
        return false;
    }

    uint8_t digest[crypto::sha256_bytes]{};
    crypto::sha256(application_buffer, file_size, digest);
    if (!activate_linux_i386_profile(path, digest, image_bias)) {
        paging::clear_user();
        clear_capability_profile();
        serial::line("LINUX_I386_COMPAT_REJECT reason=activation");
        return false;
    }

    storage::path_copy(current_application, path, sizeof(current_application));
    string_copy(current_format, "LINUX32", sizeof(current_format));
    last_exit_code = last_fault_vector = last_fault_error = last_fault_eip = 0U;
    last_fault_address = 0U;
    application_faulted = false;
    serial::write("LINUX_I386_COMPAT_START path="); serial::write(path);
    serial::write(" bias="); serial_hex(image_bias); serial::write("\r\n");
    console::write("Launching "); console::write(path); console::line(" [Linux/i386 minimal sandbox]");
    if (!configure_user_segments(image_bias)) {
        clear_capability_profile();
        paging::clear_user();
        serial::line("LINUX_I386_COMPAT_REJECT reason=segments");
        return false;
    }
    user_enter(image_bias + entry, initial_stack);
    restore_native_user_segments();
    if (application_faulted) {
        console::warning("Linux/i386 application faulted.");
        serial::line("LINUX_I386_COMPAT_FAULT");
    } else {
        console::write("Linux/i386 application exited with code ");
        console::unsigned_dec(last_exit_code); console::put('\n');
        serial::write("LINUX_I386_COMPAT_EXIT code="); serial_u32(last_exit_code); serial::write("\r\n");
    }
    clear_capability_profile();
    paging::clear_user();
    return !application_faulted;
}
} // namespace process
