namespace zmid_content_workspace_allocator {
uint32_t allocations = 0U;
bool mapping_active = false;

constexpr uint32_t workspace_pages =
    supervisor_layout::zmid_workspace_bytes / paging::page_size;
static_assert(supervisor_layout::zmid_workspace_bytes % paging::page_size == 0U);
static_assert(supervisor_layout::zmid_workspace_a >= paging::table_span);
static_assert(supervisor_layout::zmid_workspace_b + supervisor_layout::zmid_workspace_bytes <=
              paging::table_span * 2U);

uint32_t page_index(uintptr_t address) {
    return static_cast<uint32_t>((address - paging::table_span) / paging::page_size);
}

uint32_t expected_entry(uintptr_t address) {
    return static_cast<uint32_t>(address) | paging::present | paging::writable;
}

bool range_available(uintptr_t address) {
    const uint32_t first = page_index(address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        const uint32_t expected = expected_entry(address + page * paging::page_size);
        const uint32_t current = paging::user_table[first + page];
        if (current && current != expected) return false;
    }
    return true;
}

bool range_mapped_exact(uintptr_t address) {
    const uint32_t first = page_index(address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        if (paging::user_table[first + page] !=
            expected_entry(address + page * paging::page_size)) return false;
    }
    return true;
}

void map_range(uintptr_t address) {
    const uint32_t first = page_index(address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        paging::user_table[first + page] =
            expected_entry(address + page * paging::page_size);
    }
}

void unmap_range(uintptr_t address) {
    const uint32_t first = page_index(address);
    for (uint32_t page = 0U; page < workspace_pages; ++page) {
        const uint32_t expected = expected_entry(address + page * paging::page_size);
        if (paging::user_table[first + page] == expected) paging::user_table[first + page] = 0U;
    }
}

bool acquire_mapping() {
    if (!paging::enabled) return true;
    if (mapping_active) {
        return range_mapped_exact(supervisor_layout::zmid_workspace_a) &&
            range_mapped_exact(supervisor_layout::zmid_workspace_b);
    }
    if (!range_available(supervisor_layout::zmid_workspace_a) ||
        !range_available(supervisor_layout::zmid_workspace_b)) return false;
    map_range(supervisor_layout::zmid_workspace_a);
    map_range(supervisor_layout::zmid_workspace_b);
    paging::reload();
    mapping_active = true;
    return true;
}

void release_mapping() {
    if (!paging::enabled || !mapping_active) return;
    unmap_range(supervisor_layout::zmid_workspace_a);
    unmap_range(supervisor_layout::zmid_workspace_b);
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
        ? supervisor_layout::zmid_workspace_a
        : supervisor_layout::zmid_workspace_b;
    if ((address & (alignment - 1U)) != 0U) return nullptr;
    memset(reinterpret_cast<void*>(address), 0, bytes);
    return reinterpret_cast<void*>(address);
}
} // namespace zmid_content_workspace_allocator
