namespace process {
constexpr uintptr_t user_base = 0x00400000U;
constexpr uint32_t user_limit = 0x00100000U;
constexpr uint32_t stack_bytes = 16U * 1024U;
constexpr uint32_t stack_base = user_limit - stack_bytes;
constexpr uint32_t stack_top = user_limit - 16U;
constexpr uint32_t max_arguments = 8U;
constexpr uint32_t max_argument_bytes = 64U;

constexpr uint32_t syscall_exit = 0, syscall_console_write = 1, syscall_ticks = 2, syscall_file_read = 3, syscall_file_write = 4;
constexpr uint32_t syscall_file_stat = 5, syscall_version = 6, syscall_sync = 7, syscall_console_read = 8;
constexpr uint32_t error_invalid = 0xFFFFFFFFU, error_not_found = 0xFFFFFFFEU, error_no_space = 0xFFFFFFFDU;
constexpr uint32_t error_io = 0xFFFFFFFCU, error_fault = 0xFFFFFFFBU, error_unsupported = 0xFFFFFFFAU;

extern "C" uint8_t gdt_tss[];
extern "C" char __stack_top;
extern "C" void user_enter(uint32_t entry_offset, uint32_t stack_offset);

struct [[gnu::packed]] Tss {
    uint32_t previous, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
};
static_assert(sizeof(Tss) == 104);
struct [[gnu::packed]] ZexHeader { char magic[4]; uint32_t version, header_size, image_size, entry_offset, bss_size, stack_size, checksum; };
static_assert(sizeof(ZexHeader) == 32);
struct [[gnu::packed]] Elf32Header {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version, entry, program_offset, section_offset, flags;
    uint16_t header_size, program_entry_size, program_count, section_entry_size, section_count, string_section;
};
struct [[gnu::packed]] Elf32ProgramHeader { uint32_t type, offset, virtual_address, physical_address, file_size, memory_size, flags, alignment; };
static_assert(sizeof(Elf32Header) == 52 && sizeof(Elf32ProgramHeader) == 32);
struct [[gnu::packed]] UserFileInfo { uint32_t type, size, checksum; };
struct SyscallFrame { uint32_t gs, fs, es, ds; uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; uint32_t eip, cs, eflags, useresp, ss; };

Tss kernel_tss{};
uint8_t* application_buffer = nullptr;
uint32_t last_exit_code = 0, last_fault_vector = 0, last_fault_error = 0, last_fault_eip = 0;
uintptr_t last_fault_address = 0;
bool initialized = false, application_faulted = false;
char current_application[48] = "<none>";
char current_format[8] = "none";

void descriptor_set_tss(uintptr_t base, uint32_t limit) {
    gdt_tss[0] = static_cast<uint8_t>(limit); gdt_tss[1] = static_cast<uint8_t>(limit >> 8U); gdt_tss[2] = static_cast<uint8_t>(base);
    gdt_tss[3] = static_cast<uint8_t>(base >> 8U); gdt_tss[4] = static_cast<uint8_t>(base >> 16U); gdt_tss[5] = 0x89;
    gdt_tss[6] = static_cast<uint8_t>((limit >> 16U) & 0x0FU); gdt_tss[7] = static_cast<uint8_t>(base >> 24U);
}
bool user_range(uint32_t offset, uint32_t length, bool write = false) { return offset < user_limit && length <= user_limit - offset && paging::user_access(offset, length, write); }
uint8_t* user_bytes(uint32_t offset, uint32_t length, bool write = false) { return user_range(offset, length, write) ? reinterpret_cast<uint8_t*>(user_base + offset) : nullptr; }
const char* user_string(uint32_t offset, uint32_t maximum = 96U) {
    if (offset >= user_limit) return nullptr;
    const char* text = reinterpret_cast<const char*>(user_base + offset);
    uint32_t limit = user_limit - offset;
    if (limit > maximum) limit = maximum;
    for (uint32_t i = 0; i < limit; ++i) {
        if (!paging::user_access(offset + i, 1, false)) return nullptr;
        if (text[i] == 0) return text;
    }
    return nullptr;
}
uint32_t checksum(const uint8_t* data, uint32_t size) { uint32_t hash = 2166136261U; for (uint32_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; } return hash; }
void serial_u32(uint32_t value) { char digits[11]; uint32_t count = 0; do { digits[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value); while (count) serial::put(digits[--count]); }
void serial_hex(uint32_t value) { static constexpr char hex[] = "0123456789ABCDEF"; serial::write("0x"); for (int shift = 28; shift >= 0; shift -= 4) serial::put(hex[(value >> shift) & 15U]); }

void record_user_fault(uint32_t vector, uint32_t error, uint32_t eip, uintptr_t address) {
    application_faulted = true; last_fault_vector = vector; last_fault_error = error; last_fault_eip = eip; last_fault_address = address; last_exit_code = 128U + vector;
    serial::write("USER_FAULT app="); serial::write(current_application); serial::write(" vector="); serial_u32(vector);
    serial::write(" error="); serial_hex(error); serial::write(" eip="); serial_hex(eip);
    if (vector == 14) {
        serial::write(" cr2="); serial_hex(static_cast<uint32_t>(address)); serial::write(" present="); serial::put((error & 1U) ? '1' : '0');
        serial::write(" write="); serial::put((error & 2U) ? '1' : '0'); serial::write(" user="); serial::put((error & 4U) ? '1' : '0');
    }
    serial::write("\r\n");
    if (vector == 14) {
        serial::line("PAGE_FAULT_DIAGNOSTICS_OK");
        if ((error & 7U) == 7U && address >= user_base && address < user_base + user_limit && !paging::writable_at(static_cast<uint32_t>(address - user_base))) serial::line("USER_WRITE_TO_TEXT_BLOCKED");
    }
    if (vector == 13) serial::line("USER_KERNEL_ACCESS_BLOCKED");
    serial::line("USER_FAULT_RETURNED_TO_SHELL");
}

uint32_t read_console(uint8_t* output, uint32_t capacity) {
    if (!output || capacity < 2U) return error_invalid;
    serial::line("CONSOLE_READ_READY");
    uint32_t count = 0;
    for (;;) {
        const uint16_t key = key_pop_blocking();
        if (key == '\n') { console::put('\n'); break; }
        if (key == '\b') { if (count) { --count; console::put('\b'); } continue; }
        if (key > 0xFFU || key == 0 || count + 1U >= capacity) continue;
        output[count++] = static_cast<uint8_t>(key);
        console::put(static_cast<char>(key));
    }
    output[count] = 0;
    return count;
}

extern "C" uint32_t syscall_dispatch(SyscallFrame* frame) {
    if (!frame) return 1;
    const uint32_t number = frame->eax;
    if (number == syscall_exit) { last_exit_code = frame->ebx; serial::write("APP_EXIT code="); serial_u32(last_exit_code); serial::write("\r\n"); return 1; }
    if (number == syscall_console_write) {
        const uint8_t* text = user_bytes(frame->ebx, frame->ecx, false);
        if (!text) { frame->eax = error_fault; return 0; }
        for (uint32_t i = 0; i < frame->ecx; ++i) console::put(static_cast<char>(text[i]));
        frame->eax = frame->ecx;
        return 0;
    }
    if (number == syscall_console_read) { uint8_t* output = user_bytes(frame->ebx, frame->ecx, true); frame->eax = output ? read_console(output, frame->ecx) : error_fault; return 0; }
    if (number == syscall_ticks) { frame->eax = timer_ticks; return 0; }
    if (number == syscall_file_read) {
        const char* path = user_string(frame->ebx); uint8_t* output = user_bytes(frame->ecx, frame->edx, true); uint32_t actual = 0;
        if (!path || !output) frame->eax = error_fault;
        else if (!storage::read_file(path, output, frame->edx, actual)) frame->eax = error_not_found;
        else frame->eax = actual;
        return 0;
    }
    if (number == syscall_file_write) {
        const char* path = user_string(frame->ebx); const uint8_t* data = user_bytes(frame->ecx, frame->edx, false);
        if (!path || (!data && frame->edx)) frame->eax = error_fault;
        else if (!storage::write_file(path, data, frame->edx, frame->esi != 0)) frame->eax = error_io;
        else frame->eax = frame->edx;
        return 0;
    }
    if (number == syscall_file_stat) {
        const char* path = user_string(frame->ebx);
        auto* output = reinterpret_cast<UserFileInfo*>(user_bytes(frame->ecx, sizeof(UserFileInfo), true));
        storage::FileInfo info{};
        if (!path || !output) frame->eax = error_fault;
        else if (!storage::query(path, info)) frame->eax = error_not_found;
        else { output->type = info.type; output->size = info.size; output->checksum = info.checksum; frame->eax = 0; }
        return 0;
    }
    if (number == syscall_version) {
        uint8_t* output = user_bytes(frame->ebx, frame->ecx, true);
        const uint32_t length = static_cast<uint32_t>(string_length(zenov_generated::kVersion));
        if (!output) frame->eax = error_fault;
        else if (frame->ecx <= length) frame->eax = error_no_space;
        else { memcpy(output, zenov_generated::kVersion, length + 1U); frame->eax = length; }
        return 0;
    }
    if (number == syscall_sync) { frame->eax = storage::sync_metadata() ? 0U : error_io; return 0; }
    frame->eax = error_unsupported;
    return 0;
}

void init() {
    memset(&kernel_tss, 0, sizeof(kernel_tss)); kernel_tss.esp0 = reinterpret_cast<uintptr_t>(&__stack_top); kernel_tss.ss0 = 0x10;
    kernel_tss.iomap_base = static_cast<uint16_t>(sizeof(Tss)); descriptor_set_tss(reinterpret_cast<uintptr_t>(&kernel_tss), sizeof(Tss) - 1U);
    const uint16_t selector = 0x28; asm volatile("ltr %w0" : : "r"(selector));
    application_buffer = static_cast<uint8_t*>(heap::allocate(64U * 1024U, 16)); initialized = application_buffer && paging::enabled;
    serial::line(initialized ? "PROCESS_ABI_0_1_1_OK" : "Process: userspace initialization failed");
}

bool map_stack() {
    if (!paging::map_user(stack_base, stack_bytes, true)) return false;
    memset(reinterpret_cast<void*>(user_base + stack_base), 0, stack_bytes);
    return true;
}
bool ranges_overlap(uint32_t left, uint32_t left_size, uint32_t right, uint32_t right_size) { return left < right + right_size && right < left + left_size; }
bool page_ranges_overlap(uint32_t left, uint32_t left_size, uint32_t right, uint32_t right_size) {
    const uint32_t left_begin = left & ~(paging::page_size - 1U);
    const uint32_t left_end = (left + left_size + paging::page_size - 1U) & ~(paging::page_size - 1U);
    const uint32_t right_begin = right & ~(paging::page_size - 1U);
    const uint32_t right_end = (right + right_size + paging::page_size - 1U) & ~(paging::page_size - 1U);
    return left_begin < right_end && right_begin < left_end;
}
bool load_zex(const uint8_t* file, uint32_t file_size, uint32_t& entry) {
    if (file_size < sizeof(ZexHeader)) return false;
    const auto* header = reinterpret_cast<const ZexHeader*>(file);
    const char expected[4] = {'Z','E','X','1'};
    if (!storage::bytes_equal(header->magic, expected, 4) || header->version != 1 || header->header_size != sizeof(ZexHeader) ||
        header->image_size > file_size - sizeof(ZexHeader) || header->image_size + header->bss_size > stack_base || !header->image_size ||
        header->entry_offset >= header->image_size || header->stack_size > stack_bytes || checksum(file + header->header_size, header->image_size) != header->checksum) return false;
    if (header->bss_size && (header->image_size & (paging::page_size - 1U))) return false;
    paging::clear_user();
    if (!paging::map_user(0, header->image_size, true) || !map_stack()) return false;
    if (header->bss_size && !paging::map_user(header->image_size, header->bss_size, true)) return false;
    memcpy(reinterpret_cast<void*>(user_base), file + header->header_size, header->image_size);
    if (header->bss_size) memset(reinterpret_cast<void*>(user_base + header->image_size), 0, header->bss_size);
    if (!paging::protect_user(0, header->image_size, false)) return false;
    entry = header->entry_offset;
    return true;
}
bool load_elf32(const uint8_t* file, uint32_t file_size, uint32_t& entry) {
    if (file_size < sizeof(Elf32Header)) return false;
    const auto* header = reinterpret_cast<const Elf32Header*>(file);
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F' || header->ident[4] != 1 || header->ident[5] != 1 ||
        header->type != 2 || header->machine != 3 || header->version != 1 || header->header_size != sizeof(Elf32Header) ||
        header->program_entry_size != sizeof(Elf32ProgramHeader) || header->program_offset > file_size || !header->program_count ||
        header->program_count > (file_size - header->program_offset) / sizeof(Elf32ProgramHeader) || header->entry >= stack_base) return false;
    bool loaded = false, executable_entry = false;
    for (uint32_t i = 0; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const Elf32ProgramHeader*>(file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (segment->type != 1) continue;
        if (!segment->memory_size) {
            if (segment->file_size) return false;
            continue;
        }
        if (segment->file_size > segment->memory_size || segment->offset > file_size || segment->file_size > file_size - segment->offset ||
            segment->virtual_address >= stack_base || segment->memory_size > stack_base - segment->virtual_address || (segment->flags & ~7U) ||
            (segment->alignment > 1U && ((segment->alignment & (segment->alignment - 1U)) || (segment->virtual_address & (segment->alignment - 1U)) != (segment->offset & (segment->alignment - 1U))))) return false;
        for (uint32_t j = 0; j < i; ++j) {
            const auto* other = reinterpret_cast<const Elf32ProgramHeader*>(file + header->program_offset + j * sizeof(Elf32ProgramHeader));
            if (other->type != 1 || !other->memory_size) continue;
            if (ranges_overlap(segment->virtual_address, segment->memory_size, other->virtual_address, other->memory_size)) return false;
            if (((segment->flags ^ other->flags) & 2U) && page_ranges_overlap(segment->virtual_address, segment->memory_size, other->virtual_address, other->memory_size)) return false;
        }
        if ((segment->flags & 1U) && header->entry >= segment->virtual_address && header->entry < segment->virtual_address + segment->memory_size) executable_entry = true;
        loaded = true;
    }
    if (!loaded || !executable_entry) return false;
    paging::clear_user();
    for (uint32_t i = 0; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const Elf32ProgramHeader*>(file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (segment->type != 1 || !segment->memory_size) continue;
        if (!paging::map_user(segment->virtual_address, segment->memory_size, true)) return false;
        memcpy(reinterpret_cast<void*>(user_base + segment->virtual_address), file + segment->offset, segment->file_size);
        if (segment->memory_size > segment->file_size) memset(reinterpret_cast<void*>(user_base + segment->virtual_address + segment->file_size), 0, segment->memory_size - segment->file_size);
    }
    for (uint32_t i = 0; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const Elf32ProgramHeader*>(file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (segment->type == 1 && segment->memory_size && !paging::protect_user(segment->virtual_address, segment->memory_size, (segment->flags & 2U) != 0)) return false;
    }
    if (!map_stack() || paging::writable_at(header->entry)) return false;
    serial::line("PAGE_PROTECTION_OK");
    entry = header->entry;
    return true;
}

bool has_extension(const char* path) { const char* slash = path; const char* dot = nullptr; for (const char* cursor = path; *cursor; ++cursor) { if (*cursor == '/') { slash = cursor; dot = nullptr; } else if (*cursor == '.') dot = cursor; } return dot && dot > slash; }
bool read_requested(const char* input, char* requested, uint32_t& file_size) {
    char base[48];
    if (!input || !*input) return false;
    if (string_starts_with(input, "/data") || input[0] == '/') { if (!storage::path_copy(base, input, sizeof(base))) return false; }
    else if (string_starts_with(input, "apps/") || string_starts_with(input, "APPS/")) { if (!storage::path_copy(base, "/data/", sizeof(base)) || !storage::path_append(base, input, sizeof(base))) return false; }
    else if (!storage::path_copy(base, "/data/apps/", sizeof(base)) || !storage::path_append(base, input, sizeof(base))) return false;
    lower_in_place(base);
    if (has_extension(base)) { if (!storage::path_copy(requested, base, 48)) return false; return storage::read_file(requested, application_buffer, 64U * 1024U, file_size); }
    if (!storage::path_copy(requested, base, 48) || !storage::path_append(requested, ".zex", 48)) return false;
    if (storage::read_file(requested, application_buffer, 64U * 1024U, file_size)) return true;
    if (!storage::path_copy(requested, base, 48) || !storage::path_append(requested, ".elf", 48)) return false;
    return storage::read_file(requested, application_buffer, 64U * 1024U, file_size);
}
uint32_t parse_arguments(char* input, char* arguments[max_arguments]) {
    uint32_t count = 0;
    char* cursor = input;
    while (*cursor && count < max_arguments) {
        while (is_space(*cursor)) ++cursor;
        if (!*cursor) break;
        const char quote = (*cursor == '"' || *cursor == '\'') ? *cursor++ : 0;
        arguments[count++] = cursor;
        char* output = cursor;
        while (*cursor && (quote ? *cursor != quote : !is_space(*cursor))) *output++ = *cursor++;
        if (quote) { if (*cursor == quote) ++cursor; }
        else if (*cursor) ++cursor;
        *output = 0;
        while (is_space(*cursor)) ++cursor;
    }
    return count;
}
bool build_initial_stack(char* arguments[max_arguments], uint32_t count, uint32_t& initial_stack) {
    uint32_t offsets[max_arguments];
    uint32_t top = stack_top;
    for (uint32_t i = count; i > 0; --i) {
        const uint32_t length = static_cast<uint32_t>(string_length(arguments[i - 1])) + 1U;
        if (length > max_argument_bytes || length > top - stack_base) return false;
        top -= length;
        memcpy(reinterpret_cast<void*>(user_base + top), arguments[i - 1], length);
        offsets[i - 1] = top;
    }
    top &= ~3U;
    const uint32_t vector_bytes = (count + 1U) * 4U;
    if (vector_bytes + 8U > top - stack_base) return false;
    top -= vector_bytes;
    const uint32_t vector_offset = top;
    auto* vector = reinterpret_cast<uint32_t*>(user_base + vector_offset);
    for (uint32_t i = 0; i < count; ++i) vector[i] = offsets[i];
    vector[count] = 0;
    top -= 8U;
    auto* header = reinterpret_cast<uint32_t*>(user_base + top);
    header[0] = count; header[1] = vector_offset; initial_stack = top;
    return true;
}
bool run(char* input) {
    if (!initialized || !storage::ready() || !input) return false;
    char* arguments[max_arguments]{};
    const uint32_t argument_count = parse_arguments(input, arguments);
    if (!argument_count) return false;
    char requested[48]; uint32_t file_size = 0;
    if (!read_requested(arguments[0], requested, file_size)) return false;
    uint32_t entry = 0;
    const bool zex = file_size >= 4 && application_buffer[0] == 'Z' && application_buffer[1] == 'E' && application_buffer[2] == 'X' && application_buffer[3] == '1';
    const bool elf = file_size >= 4 && application_buffer[0] == 0x7F && application_buffer[1] == 'E' && application_buffer[2] == 'L' && application_buffer[3] == 'F';
    if ((zex && !load_zex(application_buffer, file_size, entry)) || (elf && !load_elf32(application_buffer, file_size, entry)) || (!zex && !elf)) { paging::clear_user(); return false; }
    uint32_t initial_stack = 0;
    if (!build_initial_stack(arguments, argument_count, initial_stack)) { paging::clear_user(); return false; }
    storage::path_copy(current_application, requested, sizeof(current_application));
    string_copy(current_format, zex ? "ZEX1" : "ELF32", sizeof(current_format));
    last_exit_code = last_fault_vector = last_fault_error = last_fault_eip = 0; last_fault_address = 0; application_faulted = false;
    serial::write(zex ? "APP_START_ZEX " : "APP_START_ELF "); serial::line(requested);
    console::write("Launching "); console::write(requested); console::line(zex ? " [ZEX1]" : " [ELF32]");
    user_enter(entry, initial_stack);
    if (application_faulted) { console::write("Application faulted: vector "); console::unsigned_dec(last_fault_vector); console::put('\n'); }
    else { console::write("Application exited with code "); console::unsigned_dec(last_exit_code); console::put('\n'); }
    paging::clear_user();
    return true;
}
} // namespace process
