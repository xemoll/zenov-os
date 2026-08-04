#ifndef ZENOV_PSX_GUEST_MEMORY_INC
#define ZENOV_PSX_GUEST_MEMORY_INC
namespace psx_guest_memory {

constexpr uintptr_t virtual_base = 0x00C00000U;
constexpr uint32_t bytes = 2U * 1024U * 1024U;
constexpr uint32_t page_count = bytes / paging::page_size;
constexpr uint32_t directory_index = static_cast<uint32_t>(virtual_base / paging::table_span);
uint32_t allocated_pages = 0U;
bool active = false;

static_assert(bytes == psx_exe::ram_bytes);
static_assert(bytes % paging::page_size == 0U);
static_assert(directory_index == 3U);

uint32_t expected_directory_entry() {
    return reinterpret_cast<uintptr_t>(paging::user_table) | paging::present | paging::writable;
}

uint32_t expected_process_directory_entry() {
    return expected_directory_entry() | paging::user;
}

bool release() {
    if (!active) return allocated_pages == 0U;
    if (paging::page_directory[directory_index] != expected_directory_entry()) return false;
    for (uint32_t page = 0U; page < allocated_pages; ++page) {
        const uint32_t entry = paging::user_table[page];
        const uintptr_t frame = entry & ~(paging::page_size - 1U);
        if ((entry & (paging::page_size - 1U)) != (paging::present | paging::writable) ||
            frame < pmm::reserved_end || frame >= pmm::managed_limit || !pmm::is_used(frame / pmm::page_size)) return false;
    }
    volatile uint8_t* memory = reinterpret_cast<volatile uint8_t*>(virtual_base);
    for (uint32_t i = 0U; i < bytes; ++i) memory[i] = 0U;
    bool okay = true;
    for (uint32_t i = allocated_pages; i > 0U; --i) {
        const uintptr_t frame = paging::user_table[i - 1U] & ~(paging::page_size - 1U);
        paging::user_table[i - 1U] = 0U;
        okay = pmm::free_frame(frame) && okay;
    }
    paging::page_directory[directory_index] = 0U;
    paging::reload();
    allocated_pages = 0U;
    active = false;
    return okay;
}

uint8_t* acquire() {
    if (!paging::enabled || !pmm::initialized || active || allocated_pages ||
        paging::page_directory[directory_index] != 0U ||
        paging::page_directory[1U] != expected_process_directory_entry() ||
        pmm::available_frames < page_count) return nullptr;
    paging::clear_user();
    for (uint32_t page = 0U; page < page_count; ++page) {
        const uintptr_t frame = pmm::allocate_frame();
        if (!frame) {
            for (uint32_t i = allocated_pages; i > 0U; --i) {
                const uintptr_t allocated = paging::user_table[i - 1U] & ~(paging::page_size - 1U);
                paging::user_table[i - 1U] = 0U;
                (void)pmm::free_frame(allocated);
            }
            allocated_pages = 0U;
            return nullptr;
        }
        paging::user_table[page] = static_cast<uint32_t>(frame) | paging::present | paging::writable;
        ++allocated_pages;
    }
    paging::page_directory[directory_index] = expected_directory_entry();
    paging::reload();
    active = true;
    memset(reinterpret_cast<void*>(virtual_base), 0, bytes);
    return reinterpret_cast<uint8_t*>(virtual_base);
}

} // namespace psx_guest_memory
#endif
