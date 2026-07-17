namespace paging {
constexpr uint32_t graphics_virtual_base = 0xC0000000U;
alignas(4096) uint32_t graphics_table[1024];

bool map_graphics_window(uintptr_t physical_base, uint32_t length) {
    if (!enabled || !physical_base || !length || (physical_base & (page_size - 1U)) || length > table_span) return false;
    const uint32_t pages = page_ceiling(length) / page_size;
    memset(graphics_table, 0, sizeof(graphics_table));
    for (uint32_t page = 0; page < pages; ++page) {
        graphics_table[page] = static_cast<uint32_t>(physical_base + page * page_size) | present | writable | 0x018U;
    }
    page_directory[graphics_virtual_base / table_span] = reinterpret_cast<uintptr_t>(graphics_table) | present | writable;
    reload();
    return true;
}
}
