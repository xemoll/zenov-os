namespace pmm {
constexpr uint32_t page_size = 4096U;
constexpr uint32_t managed_limit = 128U * 1024U * 1024U;
constexpr uint32_t frame_count = managed_limit / page_size;
constexpr uint32_t bitmap_words = frame_count / 32U;
constexpr uintptr_t reserved_end = 0x00800000U;

uint32_t frame_bitmap[bitmap_words];
uint32_t available_frames = 0;
uint32_t allocated_frames = 0;
bool initialized = false;

void mark_used(uint32_t frame) { frame_bitmap[frame / 32U] |= 1U << (frame % 32U); }
void mark_free(uint32_t frame) { frame_bitmap[frame / 32U] &= ~(1U << (frame % 32U)); }
bool is_used(uint32_t frame) { return (frame_bitmap[frame / 32U] & (1U << (frame % 32U))) != 0; }
uint32_t align_up(uint32_t value) { return (value + page_size - 1U) & ~(page_size - 1U); }
uint32_t align_down(uint32_t value) { return value & ~(page_size - 1U); }

void release_usable_range(uint32_t start, uint32_t end) {
    if (end <= start || start >= managed_limit) return;
    if (end > managed_limit) end = managed_limit;
    start = align_up(start);
    end = align_down(end);
    if (start < reserved_end) start = reserved_end;
    for (uint32_t address = start; address < end; address += page_size) {
        const uint32_t frame = address / page_size;
        if (is_used(frame)) {
            mark_free(frame);
            ++available_frames;
        }
    }
}

uintptr_t allocate_frame() {
    if (!initialized) return 0;
    for (uint32_t word = reserved_end / page_size / 32U; word < bitmap_words; ++word) {
        if (frame_bitmap[word] == 0xFFFFFFFFU) continue;
        for (uint32_t bit = 0; bit < 32U; ++bit) {
            const uint32_t frame = word * 32U + bit;
            if (frame >= frame_count || is_used(frame)) continue;
            mark_used(frame);
            --available_frames;
            ++allocated_frames;
            return static_cast<uintptr_t>(frame * page_size);
        }
    }
    return 0;
}

bool free_frame(uintptr_t address) {
    if (!initialized || address < reserved_end || address >= managed_limit || (address & (page_size - 1U)) != 0) return false;
    const uint32_t frame = static_cast<uint32_t>(address / page_size);
    if (!is_used(frame)) return false;
    mark_free(frame);
    ++available_frames;
    if (allocated_frames) --allocated_frames;
    return true;
}

void init() {
    for (uint32_t i = 0; i < bitmap_words; ++i) frame_bitmap[i] = 0xFFFFFFFFU;
    available_frames = 0;
    allocated_frames = 0;

    if (boot_info()->magic == boot_magic) {
        for (uint32_t i = 0; i < boot_info()->e820_count; ++i) {
            const auto& entry = e820_entries()[i];
            if (entry.type != 1 || entry.base_high != 0) continue;
            uint32_t end = managed_limit;
            if (entry.length_high == 0) {
                const uint32_t candidate = entry.base_low + entry.length_low;
                end = candidate < entry.base_low ? managed_limit : candidate;
            }
            release_usable_range(entry.base_low, end);
        }
    }

    initialized = available_frames != 0;
    if (!initialized) {
        serial::line("PMM_UNAVAILABLE");
        return;
    }
    const uint32_t before = available_frames;
    const uintptr_t test_frame = allocate_frame();
    if (!test_frame || allocated_frames != 1 || available_frames + 1U != before || !free_frame(test_frame) ||
        allocated_frames != 0 || available_frames != before) {
        initialized = false;
        serial::line("PMM_SELFTEST_FAILED");
        return;
    }
    serial::line("PMM_OK");
}

uint32_t free_bytes() { return available_frames * page_size; }
uint32_t allocated_bytes() { return allocated_frames * page_size; }
uint32_t managed_bytes() { return managed_limit; }
}

namespace paging {
constexpr uint32_t present = 0x001U;
constexpr uint32_t writable = 0x002U;
constexpr uint32_t user = 0x004U;
constexpr uint32_t page_size = 4096U;
constexpr uint32_t table_span = 4U * 1024U * 1024U;

alignas(4096) uint32_t page_directory[1024];
alignas(4096) uint32_t kernel_table[1024];
alignas(4096) uint32_t user_table[1024];
bool enabled = false;

void init() {
    memset(page_directory, 0, sizeof(page_directory));
    for (uint32_t i = 0; i < 1024U; ++i) {
        kernel_table[i] = i * page_size | present | writable;
        user_table[i] = (table_span + i * page_size) | present | writable | user;
    }
    page_directory[0] = reinterpret_cast<uintptr_t>(kernel_table) | present | writable;
    page_directory[1] = reinterpret_cast<uintptr_t>(user_table) | present | writable | user;

    const uintptr_t directory = reinterpret_cast<uintptr_t>(page_directory);
    asm volatile("mov %0, %%cr3" : : "r"(directory) : "memory");
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000U;
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    asm volatile("jmp 1f\n1:");
    enabled = true;
    serial::line("PAGING_OK");
}

[[maybe_unused]] uintptr_t fault_address() {
    uintptr_t address;
    asm volatile("mov %%cr2, %0" : "=r"(address));
    return address;
}
uint32_t kernel_mapped_bytes() { return table_span; }
uint32_t user_mapped_bytes() { return table_span; }
uintptr_t directory_address() { return reinterpret_cast<uintptr_t>(page_directory); }
}