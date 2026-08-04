namespace paging {
constexpr uint32_t graphics_virtual_base = 0xC0000000U;
constexpr uint32_t graphics_window_bytes = 64U * 1024U * 1024U;
constexpr uint32_t graphics_directory_entries = graphics_window_bytes / table_span;
constexpr uint32_t graphics_fallback_table_count = 4U;
constexpr uint32_t large_page_flag = 0x080U;
constexpr uint32_t cache_disable_flags = 0x018U;
constexpr uint32_t cr4_page_size_extensions = 0x010U;
alignas(4096) uint32_t graphics_tables[graphics_fallback_table_count][1024];
uint32_t graphics_mapped_bytes_value = 0U;
bool graphics_large_pages = false;

bool cpu_supports_large_pages() {
    uint32_t eax = 1U, ebx = 0U, ecx = 0U, edx = 0U;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)ecx;
    return (edx & (1U << 3U)) != 0U;
}

void enable_large_pages() {
    uint32_t cr4 = 0U;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= cr4_page_size_extensions;
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

void clear_graphics_directory() {
    const uint32_t base_directory_index = graphics_virtual_base / table_span;
    for (uint32_t entry = 0U; entry < graphics_directory_entries; ++entry)
        page_directory[base_directory_index + entry] = 0U;
}

bool map_graphics_large_pages(uintptr_t physical_base, uint32_t length) {
    if ((physical_base & (table_span - 1U)) != 0U || !cpu_supports_large_pages()) return false;
    const uint32_t entries = (length + table_span - 1U) / table_span;
    if (!entries || entries > graphics_directory_entries) return false;
    const uint32_t base_directory_index = graphics_virtual_base / table_span;
    enable_large_pages();
    for (uint32_t entry = 0U; entry < entries; ++entry) {
        const uint64_t physical = static_cast<uint64_t>(physical_base) +
            static_cast<uint64_t>(entry) * table_span;
        if (physical > 0xFFFFFFFFULL) return false;
        page_directory[base_directory_index + entry] =
            (static_cast<uint32_t>(physical) & 0xFFC00000U) |
            present | writable | cache_disable_flags | large_page_flag;
    }
    graphics_large_pages = true;
    graphics_mapped_bytes_value = entries * table_span;
    reload();
    return true;
}

bool map_graphics_small_pages(uintptr_t physical_base, uint32_t length) {
    constexpr uint32_t fallback_bytes = graphics_fallback_table_count * table_span;
    if (length > fallback_bytes) return false;
    const uint32_t pages = page_ceiling(length) / page_size;
    const uint32_t base_directory_index = graphics_virtual_base / table_span;
    memset(graphics_tables, 0, sizeof(graphics_tables));
    for (uint32_t page = 0U; page < pages; ++page) {
        const uint32_t table = page / 1024U;
        const uint32_t slot = page % 1024U;
        const uint64_t physical = static_cast<uint64_t>(physical_base) +
            static_cast<uint64_t>(page) * page_size;
        if (physical > 0xFFFFFFFFULL) return false;
        graphics_tables[table][slot] =
            static_cast<uint32_t>(physical) | present | writable | cache_disable_flags;
    }
    const uint32_t used_tables = (pages + 1023U) / 1024U;
    for (uint32_t table = 0U; table < used_tables; ++table)
        page_directory[base_directory_index + table] =
            reinterpret_cast<uintptr_t>(graphics_tables[table]) | present | writable;
    graphics_large_pages = false;
    graphics_mapped_bytes_value = page_ceiling(length);
    reload();
    return true;
}

bool map_graphics_window(uintptr_t physical_base, uint32_t length) {
    if (!enabled || !physical_base || !length ||
        (physical_base & (page_size - 1U)) != 0U ||
        length > graphics_window_bytes) return false;
    const uint64_t physical_end = static_cast<uint64_t>(physical_base) + length;
    if (physical_end > 0x100000000ULL) return false;
    clear_graphics_directory();
    if (length <= graphics_fallback_table_count * table_span)
        return map_graphics_small_pages(physical_base, length);
    if (map_graphics_large_pages(physical_base, length)) return true;
    clear_graphics_directory();
    return false;
}

uint32_t graphics_mapped_bytes() { return graphics_mapped_bytes_value; }
bool graphics_mapping_uses_large_pages() { return graphics_large_pages; }
}
