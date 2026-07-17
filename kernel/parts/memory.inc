namespace pmm {
constexpr uint32_t page_size = 4096U;
constexpr uint32_t managed_limit = 128U * 1024U * 1024U;
constexpr uint32_t frame_count = managed_limit / page_size;
constexpr uint32_t bitmap_words = frame_count / 32U;
constexpr uintptr_t reserved_end = 0x00800000U;
constexpr uint32_t self_test_frames = 16U;
uint32_t frame_bitmap[bitmap_words];
uint32_t available_frames = 0, allocated_frames = 0;
bool initialized = false;
void mark_used(uint32_t frame) { frame_bitmap[frame / 32U] |= 1U << (frame % 32U); }
void mark_free(uint32_t frame) { frame_bitmap[frame / 32U] &= ~(1U << (frame % 32U)); }
bool is_used(uint32_t frame) { return (frame_bitmap[frame / 32U] & (1U << (frame % 32U))) != 0; }
uint32_t align_up(uint32_t value) { return (value + page_size - 1U) & ~(page_size - 1U); }
uint32_t align_down(uint32_t value) { return value & ~(page_size - 1U); }
uint32_t free_bytes() { return available_frames * page_size; }
uint32_t allocated_bytes() { return allocated_frames * page_size; }
uint32_t managed_bytes() { return managed_limit; }
void release_usable_range(uint32_t start, uint32_t end) {
    if (end <= start || start >= managed_limit) return;
    if (end > managed_limit) end = managed_limit;
    start = align_up(start); end = align_down(end);
    if (start < reserved_end) start = reserved_end;
    for (uint32_t address = start; address < end; address += page_size) {
        const uint32_t frame = address / page_size;
        if (is_used(frame)) { mark_free(frame); ++available_frames; }
    }
}
uintptr_t allocate_frame() {
    if (!initialized) return 0;
    for (uint32_t word = reserved_end / page_size / 32U; word < bitmap_words; ++word) {
        if (frame_bitmap[word] == 0xFFFFFFFFU) continue;
        for (uint32_t bit = 0; bit < 32U; ++bit) {
            const uint32_t frame = word * 32U + bit;
            if (frame >= frame_count || is_used(frame)) continue;
            mark_used(frame); --available_frames; ++allocated_frames;
            return static_cast<uintptr_t>(frame * page_size);
        }
    }
    return 0;
}
bool free_frame(uintptr_t address) {
    if (!initialized || address < reserved_end || address >= managed_limit || (address & (page_size - 1U))) return false;
    const uint32_t frame = static_cast<uint32_t>(address / page_size);
    if (!is_used(frame)) return false;
    mark_free(frame); ++available_frames; if (allocated_frames) --allocated_frames; return true;
}
bool stress_test() {
    if (available_frames < self_test_frames) return false;
    const uint32_t free_before = available_frames;
    const uint32_t allocated_before = allocated_frames;
    uintptr_t frames[self_test_frames]{};
    for (uint32_t i = 0; i < self_test_frames; ++i) {
        frames[i] = allocate_frame();
        if (!frames[i] || (frames[i] & (page_size - 1U))) return false;
        for (uint32_t j = 0; j < i; ++j) if (frames[j] == frames[i]) return false;
    }
    if (available_frames + self_test_frames != free_before || allocated_frames != allocated_before + self_test_frames) return false;
    if (free_frame(frames[0] + 1U)) return false;
    for (uint32_t i = self_test_frames; i > 0; --i) if (!free_frame(frames[i - 1U])) return false;
    if (free_frame(frames[0])) return false;
    return available_frames == free_before && allocated_frames == allocated_before;
}
void init() {
    for (uint32_t i = 0; i < bitmap_words; ++i) frame_bitmap[i] = 0xFFFFFFFFU;
    available_frames = allocated_frames = 0;
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
    if (!initialized) { serial::line("PMM_UNAVAILABLE"); return; }
    if (!stress_test()) { initialized = false; serial::line("PMM_SELFTEST_FAILED"); return; }
    serial::line("PMM_STRESS_OK");
    serial::line("PMM_OK");
}
}

namespace heap {
constexpr uintptr_t start_address = 0x00100000U;
constexpr uintptr_t limit_address = 0x00300000U;
constexpr uint32_t magic = 0x48454150U;
constexpr uint32_t free_flag = 1U;
constexpr uint32_t granularity = 16U;
constexpr uint32_t stress_slots = 24U;
struct alignas(16) Block { uint32_t signature, capacity, requested, flags; Block* previous; Block* next; };
static_assert((sizeof(Block) & (granularity - 1U)) == 0);
Block* first = nullptr;
uint32_t peak_used = 0, active_allocations = 0;
uint32_t align_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1U) & ~(alignment - 1U); }
uint8_t* payload(Block* block) { return reinterpret_cast<uint8_t*>(block) + sizeof(Block); }
bool valid_block(const Block* block) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    return block && address >= start_address && address + sizeof(Block) <= limit_address && block->signature == magic && block->capacity <= limit_address - address - sizeof(Block);
}
void reset() {
    first = reinterpret_cast<Block*>(start_address);
    *first = Block{magic, static_cast<uint32_t>(limit_address - start_address - sizeof(Block)), 0, free_flag, nullptr, nullptr};
    peak_used = active_allocations = 0;
}
Block* split_after(Block* block, uint32_t left_capacity) {
    left_capacity = align_up(left_capacity, granularity);
    if (!valid_block(block) || left_capacity > block->capacity || block->capacity - left_capacity < sizeof(Block) + granularity) return nullptr;
    auto* tail = reinterpret_cast<Block*>(payload(block) + left_capacity);
    *tail = Block{magic, block->capacity - left_capacity - static_cast<uint32_t>(sizeof(Block)), 0, free_flag, block, block->next};
    if (tail->next) tail->next->previous = tail;
    block->next = tail; block->capacity = left_capacity;
    return tail;
}
uint32_t used_bytes() {
    uint32_t used = 0; for (Block* block = first; valid_block(block); block = block->next) if (!(block->flags & free_flag)) used += block->requested;
    return used;
}
[[maybe_unused]] uint32_t free_bytes() {
    uint32_t free = 0; for (Block* block = first; valid_block(block); block = block->next) if (block->flags & free_flag) free += block->capacity;
    return free;
}
uint32_t capacity_bytes() { return static_cast<uint32_t>(limit_address - start_address); }
[[maybe_unused]] uint32_t peak_bytes() { return peak_used; }
[[maybe_unused]] uint32_t allocation_count() { return active_allocations; }
bool validate() {
    uintptr_t expected = start_address; Block* previous = nullptr;
    for (Block* block = first; block; block = block->next) {
        if (!valid_block(block) || reinterpret_cast<uintptr_t>(block) != expected || block->previous != previous) return false;
        expected += sizeof(Block) + block->capacity; previous = block;
    }
    return expected == limit_address;
}
void* allocate(uint32_t bytes, uint32_t alignment = 16U) {
    if (!bytes || !alignment || (alignment & (alignment - 1U)) || alignment > 4096U) return nullptr;
    const uint32_t wanted = align_up(bytes, granularity);
    for (Block* block = first; valid_block(block); block = block->next) {
        if (!(block->flags & free_flag)) continue;
        uintptr_t aligned = (reinterpret_cast<uintptr_t>(payload(block)) + alignment - 1U) & ~(static_cast<uintptr_t>(alignment) - 1U);
        uint32_t prefix = static_cast<uint32_t>(aligned - reinterpret_cast<uintptr_t>(payload(block)));
        while (prefix && prefix < sizeof(Block) + granularity) { aligned += alignment; prefix += alignment; }
        if (prefix > block->capacity || wanted > block->capacity - prefix) continue;
        if (prefix) {
            Block* aligned_block = split_after(block, prefix - static_cast<uint32_t>(sizeof(Block)));
            if (!aligned_block) continue;
            block = aligned_block;
        }
        split_after(block, wanted);
        block->flags = 0; block->requested = bytes; ++active_allocations;
        const uint32_t used = used_bytes(); if (used > peak_used) peak_used = used;
        return payload(block);
    }
    return nullptr;
}
bool release(void* pointer) {
    if (!pointer) return false;
    Block* block = first;
    while (valid_block(block) && payload(block) != pointer) block = block->next;
    if (!valid_block(block) || (block->flags & free_flag)) return false;
    block->flags = free_flag; block->requested = 0; if (active_allocations) --active_allocations;
    if (block->next && valid_block(block->next) && (block->next->flags & free_flag)) {
        Block* next = block->next; block->capacity += static_cast<uint32_t>(sizeof(Block)) + next->capacity; block->next = next->next;
        if (block->next) block->next->previous = block;
    }
    if (block->previous && valid_block(block->previous) && (block->previous->flags & free_flag)) {
        Block* previous = block->previous; previous->capacity += static_cast<uint32_t>(sizeof(Block)) + block->capacity; previous->next = block->next;
        if (previous->next) previous->next->previous = previous;
    }
    return validate();
}
bool stress_test() {
    reset();
    void* slots[stress_slots]{};
    uint32_t sizes[stress_slots]{};
    for (uint32_t i = 0; i < stress_slots; ++i) {
        sizes[i] = 17U + ((i * 73U) % 997U);
        const uint32_t alignment = 16U << (i % 4U);
        slots[i] = allocate(sizes[i], alignment);
        if (!slots[i] || (reinterpret_cast<uintptr_t>(slots[i]) & (alignment - 1U))) return false;
        auto* bytes = static_cast<uint8_t*>(slots[i]);
        bytes[0] = static_cast<uint8_t>(0xA0U + i);
        bytes[sizes[i] - 1U] = static_cast<uint8_t>(0x50U + i);
    }
    if (!validate() || active_allocations != stress_slots) return false;
    for (uint32_t i = 0; i < stress_slots; i += 2U) {
        if (!release(slots[i])) return false;
        slots[i] = nullptr;
    }
    for (uint32_t i = 0; i < stress_slots; i += 2U) {
        sizes[i] = 33U + ((i * 41U) % 601U);
        const uint32_t alignment = 16U << ((i + 1U) % 4U);
        slots[i] = allocate(sizes[i], alignment);
        if (!slots[i] || (reinterpret_cast<uintptr_t>(slots[i]) & (alignment - 1U))) return false;
    }
    if (!validate() || active_allocations != stress_slots) return false;
    for (uint32_t i = stress_slots; i > 0; --i) if (!release(slots[i - 1U])) return false;
    return validate() && active_allocations == 0 && first && first->next == nullptr && (first->flags & free_flag) &&
           first->capacity == limit_address - start_address - sizeof(Block);
}
void init() {
    reset();
    void* a = allocate(128, 16), *b = allocate(256, 64), *c = allocate(64, 16);
    const bool aligned = a && b && c && (reinterpret_cast<uintptr_t>(b) & 63U) == 0;
    const bool freed = release(b) && release(a);
    void* d = allocate(300, 16);
    const bool reused = d == a && release(c) && release(d) && validate();
    const bool invalid = !release(reinterpret_cast<void*>(start_address + 3U)) && !release(d);
    serial::line(aligned && freed && reused ? "HEAP_REUSE_OK" : "HEAP_REUSE_FAILED");
    serial::line(reused ? "HEAP_COALESCE_OK" : "HEAP_COALESCE_FAILED");
    serial::line(invalid ? "HEAP_INVALID_FREE_BLOCKED" : "HEAP_INVALID_FREE_FAILED");
    const bool stress = stress_test();
    serial::line(stress ? "HEAP_STRESS_OK" : "HEAP_STRESS_FAILED");
    reset();
}
}

namespace paging {
constexpr uint32_t present = 0x001U, writable = 0x002U, user = 0x004U;
constexpr uint32_t page_size = 4096U, table_span = 4U * 1024U * 1024U;
alignas(4096) uint32_t page_directory[1024];
alignas(4096) uint32_t kernel_table[1024];
alignas(4096) uint32_t user_table[1024];
bool enabled = false;
uint32_t page_floor(uint32_t value) { return value & ~(page_size - 1U); }
uint32_t page_ceiling(uint32_t value) { return (value + page_size - 1U) & ~(page_size - 1U); }
void reload() { const uintptr_t directory = reinterpret_cast<uintptr_t>(page_directory); asm volatile("mov %0, %%cr3" : : "r"(directory) : "memory"); }
void clear_user() { memset(user_table, 0, sizeof(user_table)); if (enabled) reload(); }
bool map_user(uint32_t offset, uint32_t length, bool write) {
    if (!length || offset >= table_span || length > table_span - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size, end_page = page_ceiling(offset + length) / page_size;
    for (uint32_t page = first_page; page < end_page; ++page) user_table[page] = (table_span + page * page_size) | present | user | (write ? writable : 0U);
    if (enabled) reload();
    return true;
}
bool protect_user(uint32_t offset, uint32_t length, bool write) {
    if (!length || offset >= table_span || length > table_span - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size, end_page = page_ceiling(offset + length) / page_size;
    for (uint32_t page = first_page; page < end_page; ++page) {
        if (!(user_table[page] & present)) return false;
        if (write) user_table[page] |= writable; else user_table[page] &= ~writable;
    }
    if (enabled) reload();
    return true;
}
bool user_access(uint32_t offset, uint32_t length, bool write) {
    if (!length) return offset <= table_span;
    if (offset >= table_span || length > table_span - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size, end_page = page_ceiling(offset + length) / page_size;
    for (uint32_t page = first_page; page < end_page; ++page) {
        const uint32_t entry = user_table[page];
        if (!(entry & present) || !(entry & user) || (write && !(entry & writable))) return false;
    }
    return true;
}
bool writable_at(uint32_t offset) { return offset < table_span && (user_table[offset / page_size] & (present | writable | user)) == (present | writable | user); }
void init() {
    memset(page_directory, 0, sizeof(page_directory)); memset(user_table, 0, sizeof(user_table));
    for (uint32_t i = 0; i < 1024U; ++i) kernel_table[i] = i * page_size | present | writable;
    page_directory[0] = reinterpret_cast<uintptr_t>(kernel_table) | present | writable;
    page_directory[1] = reinterpret_cast<uintptr_t>(user_table) | present | writable | user;
    reload(); uint32_t cr0; asm volatile("mov %%cr0, %0" : "=r"(cr0)); cr0 |= 0x80010000U; asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory"); asm volatile("jmp 1f\n1:");
    enabled = true; serial::line("PAGING_OK");
}
uintptr_t fault_address() { uintptr_t address; asm volatile("mov %%cr2, %0" : "=r"(address)); return address; }
uint32_t kernel_mapped_bytes() { return table_span; }
uint32_t user_mapped_bytes() { uint32_t pages = 0; for (uint32_t i = 0; i < 1024U; ++i) if (user_table[i] & present) ++pages; return pages * page_size; }
uintptr_t directory_address() { return reinterpret_cast<uintptr_t>(page_directory); }
}
