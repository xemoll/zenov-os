namespace process {
constexpr uintptr_t user_base = 0x00400000;
constexpr uint32_t user_limit = 0x00100000;
constexpr uint32_t user_stack = 0x000FF000;
constexpr uint32_t syscall_exit = 0;
constexpr uint32_t syscall_write = 1;
constexpr uint32_t syscall_ticks = 2;

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
void init() {
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.esp0 = reinterpret_cast<uintptr_t>(&__stack_top);
    kernel_tss.ss0 = 0x10;
    kernel_tss.iomap_base = static_cast<uint16_t>(sizeof(Tss));
    descriptor_set_tss(reinterpret_cast<uintptr_t>(&kernel_tss), sizeof(Tss) - 1U);
    const uint16_t selector = 0x28;
    asm volatile("ltr %w0" : : "r"(selector));
    application_buffer = static_cast<uint8_t*>(heap::allocate(64U * 1024U, 16));
    initialized = application_buffer != nullptr;
    serial::line(initialized ? "Process: ring-3 ZEX runtime online" : "Process: application buffer allocation failed");
}

bool user_range(uint32_t offset, uint32_t length) {
    return offset < user_limit && length <= user_limit - offset;
}
uint32_t checksum(const uint8_t* data, uint32_t size) {
    uint32_t hash = 2166136261U;
    for (uint32_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
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
    if (number == syscall_write) {
        if (!user_range(frame->ebx, frame->ecx)) { frame->eax = 0xFFFFFFFFU; return 0; }
        const char* text = reinterpret_cast<const char*>(user_base + frame->ebx);
        for (uint32_t i = 0; i < frame->ecx; ++i) console::put(text[i]);
        frame->eax = frame->ecx;
        return 0;
    }
    if (number == syscall_ticks) {
        frame->eax = timer_ticks;
        return 0;
    }
    frame->eax = 0xFFFFFFFFU;
    return 0;
}

bool run(const char* input) {
    if (!initialized || !storage::ready()) return false;
    char requested[48];
    if (!input || !*input) return false;
    if (string_starts_with(input, "/data") || input[0] == '/') {
        if (!storage::path_copy(requested, input, sizeof(requested))) return false;
    } else if (string_starts_with(input, "apps/") || string_starts_with(input, "APPS/")) {
        if (!storage::path_copy(requested, "/data/", sizeof(requested)) ||
            !storage::path_append(requested, input, sizeof(requested))) return false;
        lower_in_place(requested);
    } else {
        if (!storage::path_copy(requested, "/data/apps/", sizeof(requested)) ||
            !storage::path_append(requested, input, sizeof(requested))) return false;
        lower_in_place(requested);
        const uint32_t length = static_cast<uint32_t>(string_length(requested));
        bool has_extension = false;
        for (uint32_t i = 0; i < length; ++i) if (requested[i] == '.') has_extension = true;
        if (!has_extension && !storage::path_append(requested, ".zex", sizeof(requested))) return false;
    }

    uint32_t file_size = 0;
    if (!storage::read_file(requested, application_buffer, 64U * 1024U, file_size) || file_size < sizeof(ZexHeader)) return false;
    const auto* header = reinterpret_cast<const ZexHeader*>(application_buffer);
    const char magic[4] = {'Z','E','X','1'};
    if (!storage::bytes_equal(header->magic, magic, 4) || header->version != 1 ||
        header->header_size != sizeof(ZexHeader) || header->image_size > file_size - sizeof(ZexHeader) ||
        header->image_size + header->bss_size > user_limit - 4096U ||
        header->entry_offset >= header->image_size || header->stack_size > user_limit / 2U) return false;
    const uint8_t* image = application_buffer + header->header_size;
    if (checksum(image, header->image_size) != header->checksum) return false;

    memset(reinterpret_cast<void*>(user_base), 0, user_limit);
    memcpy(reinterpret_cast<void*>(user_base), image, header->image_size);
    last_exit_code = 0;
    serial::write("APP_START "); serial::line(requested);
    console::write("Launching "); console::line(requested);
    user_enter(header->entry_offset, user_stack);
    console::write("Application exited with code "); console::unsigned_dec(last_exit_code); console::put('\n');
    return true;
}
} // namespace process
