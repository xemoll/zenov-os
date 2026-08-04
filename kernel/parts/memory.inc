namespace pmm {
constexpr uint32_t page_size = 4096U;
constexpr uint32_t managed_limit = 128U * 1024U * 1024U;
constexpr uint32_t frame_count = managed_limit / page_size;
constexpr uint32_t bitmap_words = frame_count / 32U;
constexpr uintptr_t reserved_end = supervisor_layout::pmm_reserved_end;
constexpr uintptr_t identity_hole_start = supervisor_layout::kernel_identity_hole_start;
constexpr uintptr_t identity_hole_end = supervisor_layout::kernel_identity_hole_end;
constexpr uint32_t self_test_frames = 16U;
constexpr uint32_t frame_bitmap_bytes = bitmap_words * sizeof(uint32_t);
static_assert(frame_bitmap_bytes == supervisor_layout::pmm_bitmap_bytes);
static_assert(supervisor_layout::pmm_bitmap + frame_bitmap_bytes <= supervisor_layout::policy_transaction);
uint32_t* frame_bitmap = reinterpret_cast<uint32_t*>(supervisor_layout::pmm_bitmap);
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
        if (address >= identity_hole_start && address < identity_hole_end) continue;
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
    if (!initialized || address < reserved_end || address >= managed_limit ||
        (address >= identity_hole_start && address < identity_hole_end) ||
        (address & (page_size - 1U))) return false;
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
    if (available_frames + self_test_frames != free_before || allocated_frames != allocated_before + self_test_frames ||
        allocated_bytes() != (allocated_before + self_test_frames) * page_size) return false;
    if (free_frame(frames[0] + 1U)) return false;
    for (uint32_t i = self_test_frames; i > 0; --i) if (!free_frame(frames[i - 1U])) return false;
    if (free_frame(frames[0])) return false;
    return available_frames == free_before && allocated_frames == allocated_before && allocated_bytes() == allocated_before * page_size;
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
constexpr uintptr_t start_address = supervisor_layout::heap_start;
constexpr uintptr_t limit_address = supervisor_layout::heap_end;
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
constexpr uint32_t kernel_table_count = pmm::managed_limit / table_span;
constexpr uint32_t supervisor_hole_directory_index = supervisor_layout::kernel_identity_hole_start / table_span;
constexpr uint32_t user_virtual_base = 0x40000000U;
constexpr uint32_t user_directory_index = user_virtual_base / table_span;
constexpr uint32_t process_window_bytes = 1U * 1024U * 1024U;
constexpr uint32_t process_page_count = process_window_bytes / page_size;
constexpr uintptr_t legacy_user_physical_base = supervisor_layout::legacy_user_physical_start;
static_assert(kernel_table_count == 32U);
static_assert(supervisor_layout::kernel_identity_hole_start % table_span == 0U);
static_assert(supervisor_layout::kernel_identity_hole_end - supervisor_layout::kernel_identity_hole_start == table_span);
static_assert(supervisor_hole_directory_index == 2U);
static_assert(user_directory_index >= kernel_table_count && user_directory_index < 1024U);
static_assert(process_window_bytes <= table_span);
static_assert(legacy_user_physical_base + process_window_bytes <= supervisor_layout::legacy_user_physical_end);

alignas(4096) uint32_t page_directory[1024];
alignas(4096) uint32_t kernel_tables[kernel_table_count][1024];
alignas(4096) uint32_t legacy_user_table[1024];
bool enabled = false;

struct AddressSpace {
    uintptr_t table_physical = 0U;
    uint32_t mapped_pages = 0U;
};
AddressSpace* active_address_space = nullptr;

uint32_t page_floor(uint32_t value) { return value & ~(page_size - 1U); }
uint32_t page_ceiling(uint32_t value) { return (value + page_size - 1U) & ~(page_size - 1U); }
uint32_t* address_space_table(const AddressSpace& space) {
    return space.table_physical ? reinterpret_cast<uint32_t*>(space.table_physical) : nullptr;
}
uint32_t* current_user_table() {
    return active_address_space ? address_space_table(*active_address_space) : legacy_user_table;
}
void reload() {
    const uintptr_t directory = reinterpret_cast<uintptr_t>(page_directory);
    asm volatile("mov %0, %%cr3" : : "r"(directory) : "memory");
}
void install_user_table(uintptr_t table_physical) {
    page_directory[user_directory_index] = table_physical ?
        static_cast<uint32_t>(table_physical) | present | writable | user : 0U;
    if (enabled) reload();
}

bool address_space_create(AddressSpace& space) {
    if (space.table_physical || !pmm::initialized) return false;
    const uintptr_t table_frame = pmm::allocate_frame();
    if (!table_frame) return false;
    space.table_physical = table_frame;
    space.mapped_pages = 0U;
    memset(reinterpret_cast<void*>(table_frame), 0, page_size);
    return true;
}
void address_space_clear(AddressSpace& space) {
    uint32_t* const table = address_space_table(space);
    if (!table) return;
    for (uint32_t page = 0U; page < process_page_count; ++page) {
        if (!(table[page] & present)) continue;
        const uintptr_t frame = table[page] & ~(static_cast<uintptr_t>(page_size) - 1U);
        table[page] = 0U;
        memset(reinterpret_cast<void*>(frame), 0, page_size);
        (void)pmm::free_frame(frame);
    }
    space.mapped_pages = 0U;
    if (&space == active_address_space && enabled) reload();
}
void address_space_destroy(AddressSpace& space) {
    if (!space.table_physical) return;
    if (&space == active_address_space) {
        active_address_space = nullptr;
        install_user_table(reinterpret_cast<uintptr_t>(legacy_user_table));
    }
    address_space_clear(space);
    const uintptr_t table_frame = space.table_physical;
    memset(reinterpret_cast<void*>(table_frame), 0, page_size);
    space = AddressSpace{};
    (void)pmm::free_frame(table_frame);
}
bool address_space_activate(AddressSpace* space) {
    if (space && !space->table_physical) return false;
    active_address_space = space;
    install_user_table(space ? space->table_physical : reinterpret_cast<uintptr_t>(legacy_user_table));
    return true;
}

void clear_user() {
    if (active_address_space) {
        address_space_clear(*active_address_space);
    } else {
        memset(legacy_user_table, 0, sizeof(legacy_user_table));
        if (enabled) reload();
    }
}
bool map_user(uint32_t offset, uint32_t length, bool write) {
    if (!length || offset >= process_window_bytes || length > process_window_bytes - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size;
    const uint32_t end_page = page_ceiling(offset + length) / page_size;
    uint32_t* const table = current_user_table();
    if (!table) return false;
    for (uint32_t page = first_page; page < end_page; ++page) {
        if (!(table[page] & present)) {
            uintptr_t frame = 0U;
            if (active_address_space) {
                frame = pmm::allocate_frame();
                if (!frame) return false;
                memset(reinterpret_cast<void*>(frame), 0, page_size);
                ++active_address_space->mapped_pages;
            } else {
                frame = legacy_user_physical_base + page * page_size;
            }
            table[page] = static_cast<uint32_t>(frame) | present | user;
        }
        if (write) table[page] |= writable;
    }
    if (enabled) reload();
    return true;
}
bool protect_user(uint32_t offset, uint32_t length, bool write) {
    if (!length || offset >= process_window_bytes || length > process_window_bytes - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size;
    const uint32_t end_page = page_ceiling(offset + length) / page_size;
    uint32_t* const table = current_user_table();
    if (!table) return false;
    for (uint32_t page = first_page; page < end_page; ++page) {
        if (!(table[page] & present)) return false;
        if (write) table[page] |= writable; else table[page] &= ~writable;
    }
    if (enabled) reload();
    return true;
}
bool user_access(uint32_t offset, uint32_t length, bool write) {
    if (!length) return offset <= process_window_bytes;
    if (offset >= process_window_bytes || length > process_window_bytes - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size;
    const uint32_t end_page = page_ceiling(offset + length) / page_size;
    const uint32_t* const table = current_user_table();
    if (!table) return false;
    for (uint32_t page = first_page; page < end_page; ++page) {
        const uint32_t entry = table[page];
        if (!(entry & present) || !(entry & user) || (write && !(entry & writable))) return false;
    }
    return true;
}
bool address_space_user_access(const AddressSpace& space, uint32_t offset,
                               uint32_t length, bool write) {
    if (!space.table_physical) return false;
    if (!length) return offset <= process_window_bytes;
    if (offset >= process_window_bytes || length > process_window_bytes - offset) return false;
    const uint32_t first_page = page_floor(offset) / page_size;
    const uint32_t end_page = page_ceiling(offset + length) / page_size;
    const uint32_t* const table = address_space_table(space);
    if (!table) return false;
    for (uint32_t page = first_page; page < end_page; ++page) {
        const uint32_t entry = table[page];
        if (!(entry & present) || !(entry & user) || (write && !(entry & writable))) return false;
    }
    return true;
}

bool address_space_write(AddressSpace& space, uint32_t offset,
                         const void* source, uint32_t length) {
    if ((!source && length) || !address_space_user_access(space, offset, length, true)) return false;
    const auto* input = static_cast<const uint8_t*>(source);
    uint32_t copied = 0U;
    uint32_t* const table = address_space_table(space);
    while (copied < length) {
        const uint32_t current = offset + copied;
        const uint32_t page = current / page_size;
        const uint32_t within = current % page_size;
        uint32_t chunk = page_size - within;
        if (chunk > length - copied) chunk = length - copied;
        const uintptr_t frame = table[page] & ~(static_cast<uintptr_t>(page_size) - 1U);
        memcpy(reinterpret_cast<void*>(frame + within), input + copied, chunk);
        copied += chunk;
    }
    return true;
}

bool writable_at(uint32_t offset) {
    const uint32_t* const table = current_user_table();
    return table && offset < process_window_bytes &&
        (table[offset / page_size] & (present | writable | user)) == (present | writable | user);
}

void init() {
    memset(page_directory, 0, sizeof(page_directory));
    memset(kernel_tables, 0, sizeof(kernel_tables));
    memset(legacy_user_table, 0, sizeof(legacy_user_table));
    for (uint32_t table = 0U; table < kernel_table_count; ++table) {
        for (uint32_t page = 0U; page < 1024U; ++page) {
            const uint32_t address = table * table_span + page * page_size;
            kernel_tables[table][page] = address | present | writable;
        }
        if (table != supervisor_hole_directory_index) {
            page_directory[table] = reinterpret_cast<uintptr_t>(kernel_tables[table]) | present | writable;
        }
    }
    for (uint32_t task = 0U; task < supervisor_layout::task_stack_count; ++task) {
        const uintptr_t guard = supervisor_layout::task_stack_region_start +
            task * supervisor_layout::task_stack_stride;
        kernel_tables[guard / table_span][(guard % table_span) / page_size] = 0U;
    }
    page_directory[user_directory_index] = reinterpret_cast<uintptr_t>(legacy_user_table) | present | writable | user;
    reload();
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000U;
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    asm volatile("jmp 1f\n1:");
    enabled = true;
    serial::line("PAGING_OK");
    serial::line("KERNEL_IDENTITY_MAP_OK bytes=134217728 supervisor-only=yes excluded=0x00800000-0x00c00000");
    serial::line("PMM_IDENTITY_HOLE_RESERVED_OK bytes=4194304 reason=zmid-supervisor-pde");
    serial::line("USER_HIGH_HALF_WINDOW_OK base=0x40000000 bytes=1048576");
    serial::line("TASK_KERNEL_GUARD_PAGES_OK count=8");
}
uintptr_t fault_address() { uintptr_t address; asm volatile("mov %%cr2, %0" : "=r"(address)); return address; }
uint32_t kernel_mapped_bytes() { return pmm::managed_limit; }
uint32_t user_mapped_bytes() {
    uint32_t pages = 0U;
    const uint32_t* const table = current_user_table();
    if (!table) return 0U;
    for (uint32_t i = 0U; i < process_page_count; ++i) if (table[i] & present) ++pages;
    return pages * page_size;
}
uintptr_t directory_address() { return reinterpret_cast<uintptr_t>(page_directory); }
[[maybe_unused]] uintptr_t active_user_table_address() {
    const uint32_t entry = page_directory[user_directory_index];
    return entry & ~(static_cast<uintptr_t>(page_size) - 1U);
}
} // namespace paging
