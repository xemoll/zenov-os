namespace process {
constexpr uintptr_t user_base = 0x00400000;
constexpr uint32_t user_limit = 0x00100000;
constexpr uint32_t user_stack = 0x000FF000;

constexpr uint32_t syscall_exit = 0;
constexpr uint32_t syscall_console_write = 1;
constexpr uint32_t syscall_ticks = 2;
constexpr uint32_t syscall_file_read = 3;
constexpr uint32_t syscall_file_write = 4;
constexpr uint32_t syscall_file_stat = 5;
constexpr uint32_t syscall_version = 6;
constexpr uint32_t syscall_sync = 7;
constexpr uint32_t syscall_error = 0xFFFFFFFFU;

extern "C" uint8_t gdt_tss[];
extern "C" char __stack_top;
extern "C" void user_enter(uint32_t entry_offset, uint32_t stack_offset);

struct [[gnu::packed]] Tss {
    uint32_t previous;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
};
static_assert(sizeof(Tss) == 104);

struct [[gnu::packed]] ZexHeader {
    char magic[4];
    uint32_t version;
    uint32_t header_size;
    uint32_t image_size;
    uint32_t entry_offset;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t checksum;
};
static_assert(sizeof(ZexHeader) == 32);

struct [[gnu::packed]] Elf32Header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t program_offset;
    uint32_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t string_section;
};
struct [[gnu::packed]] Elf32ProgramHeader {
    uint32_t type;
    uint32_t offset;
    uint32_t virtual_address;
    uint32_t physical_address;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t flags;
    uint32_t alignment;
};
static_assert(sizeof(Elf32Header) == 52);
static_assert(sizeof(Elf32ProgramHeader) == 32);

struct [[gnu::packed]] UserFileInfo {
    uint32_t type;
    uint32_t size;
    uint32_t checksum;
};

struct SyscallFrame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, useresp, ss;
};

Tss kernel_tss{};
uint8_t* application_buffer = nullptr;
uint32_t last_exit_code = 0;
bool initialized = false;

void descriptor_set_tss(uintptr_t base, uint32_t limit) {
    gdt_tss[0] = static_cast<uint8_t>(limit);
    gdt_tss[1] = static_cast<uint8_t>(limit >> 8U);
    gdt_tss[2] = static_cast<uint8_t>(base);
    gdt_tss[3] = static_cast<uint8_t>(base >> 8U);
    gdt_tss[4] = static_cast<uint8_t>(base >> 16U);
    gdt_tss[5] = 0x89;
    gdt_tss[6] = static_cast<uint8_t>((limit >> 16U) & 0x0FU);
    gdt_tss[7] = static_cast<uint8_t>(base >> 24U);
}

bool user_range(uint32_t offset, uint32_t length) {
    return offset < user_limit && length <= user_limit - offset;
}
uint8_t* user_bytes(uint32_t offset, uint32_t length) {
    return user_range(offset, length) ? reinterpret_cast<uint8_t*>(user_base + offset) : nullptr;
}
const char* user_string(uint32_t offset, uint32_t maximum = 96U) {
    if (!user_range(offset, 1)) return nullptr;
    const char* text = reinterpret_cast<const char*>(user_base + offset);
    uint32_t limit = user_limit - offset;
    if (limit > maximum) limit = maximum;
    for (uint32_t i = 0; i < limit; ++i) if (text[i] == 0) return text;
    return nullptr;
}
uint32_t checksum(const uint8_t* data, uint32_t size) {
    uint32_t hash = 2166136261U;
    for (uint32_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
}

void init() {
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.esp0 = reinterpret_cast<uintptr_t>(&__stack_top);
    kernel_tss.ss0 = 0x10;
    kernel_tss.iomap_base = static_cast<uint16_t>(sizeof(Tss));
    descriptor_set_tss(reinterpret_cast<uintptr_t>(&kernel_tss), sizeof(Tss) - 1U);
    const uint16_t selector = 0x28;
    asm volatile("ltr %w0" : : "r"(selector));
    application_buffer = static_cast<uint8_t*>(heap::allocate(64U * 1024U, 16));
    initialized = application_buffer != nullptr && paging::enabled;
    serial::line(initialized ? "PROCESS_ABI_0_1_1_OK" : "Process: userspace initialization failed");
}

extern "C" uint32_t syscall_dispatch(SyscallFrame* frame) {
    if (!frame) return 1;
    const uint32_t number = frame->eax;
    if (number == syscall_exit) {
        last_exit_code = frame->ebx;
        serial::write("APP_EXIT code=");
        char digits[11]; uint32_t count = 0, value = last_exit_code;
        do { digits[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(digits));
        while (count) serial::put(digits[--count]);
        serial::write("\r\n");
        return 1;
    }
    if (number == syscall_console_write) {
        const uint8_t* text = user_bytes(frame->ebx, frame->ecx);
        if (!text) { frame->eax = syscall_error; return 0; }
        for (uint32_t i = 0; i < frame->ecx; ++i) console::put(static_cast<char>(text[i]));
        frame->eax = frame->ecx;
        return 0;
    }
    if (number == syscall_ticks) {
        frame->eax = timer_ticks;
        return 0;
    }
    if (number == syscall_file_read) {
        const char* path = user_string(frame->ebx);
        uint8_t* output = user_bytes(frame->ecx, frame->edx);
        uint32_t actual = 0;
        if (!path || !output || !storage::read_file(path, output, frame->edx, actual)) frame->eax = syscall_error;
        else frame->eax = actual;
        return 0;
    }
    if (number == syscall_file_write) {
        const char* path = user_string(frame->ebx);
        const uint8_t* data = user_bytes(frame->ecx, frame->edx);
        if (!path || (!data && frame->edx) || !storage::write_file(path, data, frame->edx, frame->esi != 0)) frame->eax = syscall_error;
        else frame->eax = frame->edx;
        return 0;
    }
    if (number == syscall_file_stat) {
        const char* path = user_string(frame->ebx);
        auto* output = reinterpret_cast<UserFileInfo*>(user_bytes(frame->ecx, sizeof(UserFileInfo)));
        storage::FileInfo info{};
        if (!path || !output || !storage::query(path, info)) frame->eax = syscall_error;
        else {
            output->type = info.type;
            output->size = info.size;
            output->checksum = info.checksum;
            frame->eax = 0;
        }
        return 0;
    }
    if (number == syscall_version) {
        uint8_t* output = user_bytes(frame->ebx, frame->ecx);
        const uint32_t length = static_cast<uint32_t>(string_length(zenov_generated::kVersion));
        if (!output || frame->ecx <= length) frame->eax = syscall_error;
        else {
            memcpy(output, zenov_generated::kVersion, length + 1U);
            frame->eax = length;
        }
        return 0;
    }
    if (number == syscall_sync) {
        frame->eax = storage::sync_metadata() ? 0U : syscall_error;
        return 0;
    }
    frame->eax = syscall_error;
    return 0;
}

bool load_zex(const uint8_t* file, uint32_t file_size, uint32_t& entry) {
    if (file_size < sizeof(ZexHeader)) return false;
    const auto* header = reinterpret_cast<const ZexHeader*>(file);
    const char magic[4] = {'Z','E','X','1'};
    if (!storage::bytes_equal(header->magic, magic, 4) || header->version != 1 ||
        header->header_size != sizeof(ZexHeader) || header->image_size > file_size - sizeof(ZexHeader) ||
        header->image_size + header->bss_size > user_limit - 4096U ||
        header->entry_offset >= header->image_size || header->stack_size > user_limit / 2U) return false;
    const uint8_t* image = file + header->header_size;
    if (checksum(image, header->image_size) != header->checksum) return false;
    memcpy(reinterpret_cast<void*>(user_base), image, header->image_size);
    memset(reinterpret_cast<void*>(user_base + header->image_size), 0, header->bss_size);
    entry = header->entry_offset;
    return true;
}

bool load_elf32(const uint8_t* file, uint32_t file_size, uint32_t& entry) {
    if (file_size < sizeof(Elf32Header)) return false;
    const auto* header = reinterpret_cast<const Elf32Header*>(file);
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 1 || header->ident[5] != 1 || header->type != 2 || header->machine != 3 || header->version != 1 ||
        header->header_size != sizeof(Elf32Header) || header->program_entry_size != sizeof(Elf32ProgramHeader) ||
        header->program_offset > file_size || header->program_count == 0 ||
        header->program_count > (file_size - header->program_offset) / sizeof(Elf32ProgramHeader) || header->entry >= user_limit) return false;

    bool loaded = false;
    for (uint32_t i = 0; i < header->program_count; ++i) {
        const auto* program = reinterpret_cast<const Elf32ProgramHeader*>(file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (program->type != 1) continue;
        if (program->file_size > program->memory_size || program->offset > file_size ||
            program->file_size > file_size - program->offset || program->virtual_address >= user_limit ||
            program->memory_size > user_limit - program->virtual_address) return false;
        memcpy(reinterpret_cast<void*>(user_base + program->virtual_address), file + program->offset, program->file_size);
        if (program->memory_size > program->file_size) {
            memset(reinterpret_cast<void*>(user_base + program->virtual_address + program->file_size), 0,
                   program->memory_size - program->file_size);
        }
        loaded = true;
    }
    if (!loaded) return false;
    entry = header->entry;
    return true;
}

bool has_extension(const char* path) {
    const char* slash = path;
    const char* dot = nullptr;
    for (const char* cursor = path; *cursor; ++cursor) {
        if (*cursor == '/') { slash = cursor; dot = nullptr; }
        else if (*cursor == '.') dot = cursor;
    }
    return dot && dot > slash;
}

bool read_requested(const char* input, char* requested, uint32_t& file_size) {
    char base[48];
    if (!input || !*input) return false;
    if (string_starts_with(input, "/data") || input[0] == '/') {
        if (!storage::path_copy(base, input, sizeof(base))) return false;
    } else if (string_starts_with(input, "apps/") || string_starts_with(input, "APPS/")) {
        if (!storage::path_copy(base, "/data/", sizeof(base)) || !storage::path_append(base, input, sizeof(base))) return false;
    } else {
        if (!storage::path_copy(base, "/data/apps/", sizeof(base)) || !storage::path_append(base, input, sizeof(base))) return false;
    }
    lower_in_place(base);
    if (has_extension(base)) {
        if (!storage::path_copy(requested, base, 48)) return false;
        return storage::read_file(requested, application_buffer, 64U * 1024U, file_size);
    }
    if (!storage::path_copy(requested, base, 48) || !storage::path_append(requested, ".zex", 48)) return false;
    if (storage::read_file(requested, application_buffer, 64U * 1024U, file_size)) return true;
    if (!storage::path_copy(requested, base, 48) || !storage::path_append(requested, ".elf", 48)) return false;
    return storage::read_file(requested, application_buffer, 64U * 1024U, file_size);
}

bool run(const char* input) {
    if (!initialized || !storage::ready()) return false;
    char requested[48];
    uint32_t file_size = 0;
    if (!read_requested(input, requested, file_size)) return false;

    memset(reinterpret_cast<void*>(user_base), 0, user_limit);
    uint32_t entry = 0;
    const bool zex = file_size >= 4 && application_buffer[0] == 'Z' && application_buffer[1] == 'E' &&
                     application_buffer[2] == 'X' && application_buffer[3] == '1';
    const bool elf = file_size >= 4 && application_buffer[0] == 0x7F && application_buffer[1] == 'E' &&
                     application_buffer[2] == 'L' && application_buffer[3] == 'F';
    if ((zex && !load_zex(application_buffer, file_size, entry)) ||
        (elf && !load_elf32(application_buffer, file_size, entry)) || (!zex && !elf)) return false;

    last_exit_code = 0;
    serial::write(zex ? "APP_START_ZEX " : "APP_START_ELF "); serial::line(requested);
    console::write("Launching "); console::write(requested); console::line(zex ? " [ZEX1]" : " [ELF32]");
    user_enter(entry, user_stack);
    console::write("Application exited with code "); console::unsigned_dec(last_exit_code); console::put('\n');
    return true;
}
} // namespace process
