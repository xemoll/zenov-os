namespace zmid_content_workspace_allocator {
uint32_t allocations = 0U;
bool mapping_active = false;
bool mapping_fault = false;
alignas(4096) uint32_t supervisor_table[1024]{};

constexpr uint32_t workspace_pages =
    supervisor_layout::zmid_workspace_bytes / paging::page_size;
constexpr uint32_t directory_index = static_cast<uint32_t>(
    supervisor_layout::zmid_workspace_virtual_a / paging::table_span);
constexpr uintptr_t virtual_table_base =
    static_cast<uintptr_t>(directory_index) * paging::table_span;
static_assert(supervisor_layout::zmid_workspace_bytes % paging::page_size == 0U);
static_assert(directory_index == 2U);
static_assert(supervisor_layout::zmid_workspace_virtual_a == virtual_table_base);
static_assert(supervisor_layout::zmid_workspace_virtual_end <=
              virtual_table_base + paging::table_span);

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

bool table_empty() {
    for (uint32_t entry : supervisor_table) if (entry) return false;
    return true;
}

bool range_mapped_exact(uintptr_t virtual_address,
                        uintptr_t physical_address) {
    const uint32_t first = page_index(virtual_address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        if (supervisor_table[first + page] !=
            expected_entry(physical_address + page * paging::page_size)) {
            return false;
        }
    }
    return true;
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

bool mapping_exact() {
    return paging::page_directory[directory_index] == expected_directory_entry() &&
        range_mapped_exact(supervisor_layout::zmid_workspace_virtual_a,
                           supervisor_layout::zmid_workspace_physical_a) &&
        range_mapped_exact(supervisor_layout::zmid_workspace_virtual_b,
                           supervisor_layout::zmid_workspace_physical_b);
}

bool acquire_mapping() {
    if (!paging::enabled || mapping_fault) return false;
    if (mapping_active) return mapping_exact();
    if (paging::page_directory[directory_index] || !table_empty()) return false;

    map_range(supervisor_layout::zmid_workspace_virtual_a,
              supervisor_layout::zmid_workspace_physical_a);
    map_range(supervisor_layout::zmid_workspace_virtual_b,
              supervisor_layout::zmid_workspace_physical_b);
    paging::page_directory[directory_index] = expected_directory_entry();
    paging::reload();
    mapping_active = true;
    return mapping_exact();
}

void release_mapping() {
    if (!paging::enabled || !mapping_active) return;
    if (!mapping_exact()) {
        mapping_fault = true;
        return;
    }
    clear_range(supervisor_layout::zmid_workspace_virtual_a);
    clear_range(supervisor_layout::zmid_workspace_virtual_b);
    if (!table_empty() ||
        paging::page_directory[directory_index] != expected_directory_entry()) {
        mapping_fault = true;
        return;
    }
    paging::page_directory[directory_index] = 0U;
    paging::reload();
    mapping_active = false;
}

void* allocate(uint32_t bytes, uint32_t alignment) {
    if (bytes != supervisor_layout::zmid_workspace_bytes || !alignment ||
        (alignment & (alignment - 1U)) != 0U || alignment > paging::page_size ||
        allocations >= 2U || !acquire_mapping()) {
        return nullptr;
    }
    const uintptr_t address = allocations++ == 0U
        ? supervisor_layout::zmid_workspace_virtual_a
        : supervisor_layout::zmid_workspace_virtual_b;
    if ((address & (alignment - 1U)) != 0U) return nullptr;
    memset(reinterpret_cast<void*>(address), 0, bytes);
    return reinterpret_cast<void*>(address);
}
} // namespace zmid_content_workspace_allocator
