namespace paging {
constexpr uint32_t graphics_virtual_base = 0xC0000000U;
constexpr uint32_t graphics_table_count = 4U;
constexpr uint32_t graphics_window_bytes = graphics_table_count * table_span;
alignas(4096) uint32_t graphics_tables[graphics_table_count][1024];

bool map_graphics_window(uintptr_t physical_base, uint32_t length) {
    if (!enabled || !physical_base || !length || (physical_base & (page_size - 1U)) || length > graphics_window_bytes) return false;
    const uint32_t pages = page_ceiling(length) / page_size;
    const uint32_t base_directory_index = graphics_virtual_base / table_span;
    memset(graphics_tables, 0, sizeof(graphics_tables));
    for (uint32_t table = 0; table < graphics_table_count; ++table) page_directory[base_directory_index + table] = 0;
    for (uint32_t page = 0; page < pages; ++page) {
        const uint32_t table = page / 1024U;
        const uint32_t slot = page % 1024U;
        graphics_tables[table][slot] = static_cast<uint32_t>(physical_base + page * page_size) | present | writable | 0x018U;
    }
    const uint32_t used_tables = (pages + 1023U) / 1024U;
    for (uint32_t table = 0; table < used_tables; ++table) {
        page_directory[base_directory_index + table] = reinterpret_cast<uintptr_t>(graphics_tables[table]) | present | writable;
    }
    reload();
    return true;
}
}