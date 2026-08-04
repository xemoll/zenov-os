namespace zmid_content_workspace_allocator {
enum class Failure : uint8_t {
    none,
    paging_disabled,
    latched_fault,
    active_mismatch,
    directory_busy,
    table_busy,
    post_map_mismatch,
    allocation_arguments,
    allocation_limit,
    allocation_mapping,
    allocation_alignment
};

uint32_t allocations = 0U;
bool mapping_active = false;
bool mapping_fault = false;
Failure last_failure = Failure::none;
uint32_t failure_index = 0U;
uint32_t failure_actual = 0U;
uint32_t failure_expected = 0U;
alignas(4096) uint32_t supervisor_table[1024]{};

constexpr uint32_t workspace_pages =
    supervisor_layout::zmid_workspace_bytes / paging::page_size;
constexpr uint32_t directory_index = static_cast<uint32_t>(
    supervisor_layout::zmid_workspace_virtual_a / paging::table_span);
constexpr uintptr_t virtual_table_base =
    static_cast<uintptr_t>(directory_index) * paging::table_span;
constexpr uint32_t hardware_accessed = 0x020U;
constexpr uint32_t hardware_dirty = 0x040U;
constexpr uint32_t pde_hardware_mask = hardware_accessed;
constexpr uint32_t pte_hardware_mask = hardware_accessed | hardware_dirty;
static_assert(supervisor_layout::zmid_workspace_bytes % paging::page_size == 0U);
static_assert(directory_index == 2U);
static_assert(supervisor_layout::zmid_workspace_virtual_a == virtual_table_base);
static_assert(supervisor_layout::zmid_workspace_virtual_end <=
              virtual_table_base + paging::table_span);

void record_failure(Failure failure, uint32_t index = 0U,
                    uint32_t actual = 0U, uint32_t expected = 0U) {
    last_failure = failure;
    failure_index = index;
    failure_actual = actual;
    failure_expected = expected;
}

void clear_failure() {
    record_failure(Failure::none);
}

const char* failure_name() {
    switch (last_failure) {
        case Failure::none: return "none";
        case Failure::paging_disabled: return "paging-disabled";
        case Failure::latched_fault: return "latched-fault";
        case Failure::active_mismatch: return "active-mismatch";
        case Failure::directory_busy: return "directory-busy";
        case Failure::table_busy: return "table-busy";
        case Failure::post_map_mismatch: return "post-map-mismatch";
        case Failure::allocation_arguments: return "allocation-arguments";
        case Failure::allocation_limit: return "allocation-limit";
        case Failure::allocation_mapping: return "allocation-mapping";
        case Failure::allocation_alignment: return "allocation-alignment";
    }
    return "unknown";
}

uint32_t page_index(uintptr_t virtual_address) {
    return static_cast<uint32_t>((virtual_address - virtual_table_base) /
                                 paging::page_size);
}

uint32_t expected_entry(uintptr_t physical_address) {
    return static_cast<uint32_t>(physical_address) |
        paging::present | paging::writable;
}

uint32_t expected_directory_entry() {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(supervisor_table)) |
        paging::present | paging::writable;
}

bool entry_matches(uint32_t actual, uint32_t expected,
                   uint32_t hardware_mask) {
    return (actual & ~hardware_mask) == expected;
}

bool directory_entry_matches(uint32_t actual) {
    return entry_matches(actual, expected_directory_entry(), pde_hardware_mask);
}

bool table_empty(uint32_t& occupied_index, uint32_t& occupied_value) {
    for (uint32_t index = 0U; index < 1024U; ++index) {
        if (!supervisor_table[index]) continue;
        occupied_index = index;
        occupied_value = supervisor_table[index];
        return false;
    }
    occupied_index = 0U;
    occupied_value = 0U;
    return true;
}

[[maybe_unused]] bool table_empty() {
    uint32_t index = 0U, value = 0U;
    return table_empty(index, value);
}

bool range_mapped_exact(uintptr_t virtual_address,
                        uintptr_t physical_address,
                        uint32_t& mismatch_index,
                        uint32_t& actual,
                        uint32_t& expected) {
    const uint32_t first = page_index(virtual_address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        expected = expected_entry(physical_address + page * paging::page_size);
        actual = supervisor_table[first + page];
        if (entry_matches(actual, expected, pte_hardware_mask)) continue;
        mismatch_index = first + page;
        return false;
    }
    mismatch_index = 0U;
    actual = expected = 0U;
    return true;
}

[[maybe_unused]] bool range_mapped_exact(uintptr_t virtual_address,
                                         uintptr_t physical_address) {
    uint32_t index = 0U, actual = 0U, expected = 0U;
    return range_mapped_exact(virtual_address, physical_address,
                              index, actual, expected);
}

void map_range(uintptr_t virtual_address, uintptr_t physical_address) {
    const uint32_t first = page_index(virtual_address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        supervisor_table[first + page] =
            expected_entry(physical_address + page * paging::page_size);
    }
}

void clear_range(uintptr_t virtual_address) {
    const uint32_t first = page_index(virtual_address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        supervisor_table[first + page] = 0U;
    }
}

bool mapping_exact(uint32_t& mismatch_index,
                   uint32_t& actual, uint32_t& expected) {
    expected = expected_directory_entry();
    actual = paging::page_directory[directory_index];
    if (!directory_entry_matches(actual)) {
        mismatch_index = directory_index;
        return false;
    }
    if (!range_mapped_exact(supervisor_layout::zmid_workspace_virtual_a,
                            supervisor_layout::zmid_workspace_physical_a,
                            mismatch_index, actual, expected)) return false;
    return range_mapped_exact(supervisor_layout::zmid_workspace_virtual_b,
                              supervisor_layout::zmid_workspace_physical_b,
                              mismatch_index, actual, expected);
}

[[maybe_unused]] bool mapping_exact() {
    uint32_t index = 0U, actual = 0U, expected = 0U;
    return mapping_exact(index, actual, expected);
}

bool acquire_mapping() {
    if (!paging::enabled) {
        record_failure(Failure::paging_disabled);
        return false;
    }
    if (mapping_fault) {
        record_failure(Failure::latched_fault);
        return false;
    }
    if (mapping_active) {
        uint32_t index = 0U, actual = 0U, expected = 0U;
        if (mapping_exact(index, actual, expected)) {
            clear_failure();
            return true;
        }
        mapping_fault = true;
        record_failure(Failure::active_mismatch, index, actual, expected);
        return false;
    }
    const uint32_t directory_value = paging::page_directory[directory_index];
    if (directory_value) {
        mapping_fault = true;
        record_failure(Failure::directory_busy, directory_index,
                       directory_value, 0U);
        return false;
    }
    uint32_t occupied_index = 0U, occupied_value = 0U;
    if (!table_empty(occupied_index, occupied_value)) {
        mapping_fault = true;
        record_failure(Failure::table_busy, occupied_index,
                       occupied_value, 0U);
        return false;
    }

    map_range(supervisor_layout::zmid_workspace_virtual_a,
              supervisor_layout::zmid_workspace_physical_a);
    map_range(supervisor_layout::zmid_workspace_virtual_b,
              supervisor_layout::zmid_workspace_physical_b);
    paging::page_directory[directory_index] = expected_directory_entry();
    paging::reload();
    mapping_active = true;
    uint32_t index = 0U, actual = 0U, expected = 0U;
    if (mapping_exact(index, actual, expected)) {
        clear_failure();
        return true;
    }
    mapping_fault = true;
    record_failure(Failure::post_map_mismatch, index, actual, expected);
    return false;
}

void release_mapping() {
    if (!paging::enabled || !mapping_active) return;
    uint32_t index = 0U, actual = 0U, expected = 0U;
    if (!mapping_exact(index, actual, expected)) {
        mapping_fault = true;
        record_failure(Failure::active_mismatch, index, actual, expected);
        return;
    }
    clear_range(supervisor_layout::zmid_workspace_virtual_a);
    clear_range(supervisor_layout::zmid_workspace_virtual_b);
    uint32_t occupied_index = 0U, occupied_value = 0U;
    const uint32_t directory_value = paging::page_directory[directory_index];
    if (!table_empty(occupied_index, occupied_value) ||
        !directory_entry_matches(directory_value)) {
        mapping_fault = true;
        record_failure(Failure::active_mismatch,
                       occupied_value ? occupied_index : directory_index,
                       occupied_value ? occupied_value : directory_value,
                       occupied_value ? 0U : expected_directory_entry());
        return;
    }
    paging::page_directory[directory_index] = 0U;
    paging::reload();
    mapping_active = false;
    clear_failure();
}

void* allocate(uint32_t bytes, uint32_t alignment) {
    if (bytes != supervisor_layout::zmid_workspace_bytes || !alignment ||
        (alignment & (alignment - 1U)) != 0U || alignment > paging::page_size) {
        record_failure(Failure::allocation_arguments, 0U, bytes, alignment);
        return nullptr;
    }
    if (allocations >= 2U) {
        record_failure(Failure::allocation_limit, allocations, allocations, 2U);
        return nullptr;
    }
    if (!acquire_mapping()) {
        if (last_failure == Failure::none) {
            record_failure(Failure::allocation_mapping);
        }
        return nullptr;
    }
    const uintptr_t address = allocations++ == 0U
        ? supervisor_layout::zmid_workspace_virtual_a
        : supervisor_layout::zmid_workspace_virtual_b;
    if ((address & (alignment - 1U)) != 0U) {
        record_failure(Failure::allocation_alignment, allocations - 1U,
                       static_cast<uint32_t>(address), alignment);
        return nullptr;
    }
    memset(reinterpret_cast<void*>(address), 0, bytes);
    clear_failure();
    return reinterpret_cast<void*>(address);
}
} // namespace zmid_content_workspace_allocator
