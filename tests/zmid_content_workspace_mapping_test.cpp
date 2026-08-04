#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include "../kernel/parts/supervisor_layout.inc"

namespace paging {
constexpr std::uint32_t present = 0x001U;
constexpr std::uint32_t writable = 0x002U;
constexpr std::uint32_t user = 0x004U;
constexpr std::uint32_t page_size = 4096U;
constexpr std::uint32_t table_span = 4U * 1024U * 1024U;
std::uint32_t page_directory[1024]{};
bool enabled = true;
std::uint32_t reload_count = 0U;
void reload() { ++reload_count; }
}

#include "../kernel/parts/zmid_content_workspace.inc"

namespace {
using zmid_content_workspace_allocator::directory_index;
using zmid_content_workspace_allocator::supervisor_table;
using zmid_content_workspace_allocator::workspace_pages;

bool map_host_workspace() {
    const std::size_t bytes = supervisor_layout::zmid_workspace_bytes * 2U;
    void* const wanted = reinterpret_cast<void*>(
        supervisor_layout::zmid_workspace_virtual_a);
    void* mapped = mmap(wanted, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                        -1, 0);
    return mapped == wanted;
}

std::uint32_t first_index(std::uintptr_t virtual_address) {
    return static_cast<std::uint32_t>(
        (virtual_address - supervisor_layout::zmid_workspace_virtual_a) /
        paging::page_size);
}

bool range_is_zero(std::uintptr_t virtual_address) {
    const auto first = first_index(virtual_address);
    for (std::uint32_t page = 0; page < workspace_pages; ++page) {
        if (supervisor_table[first + page] != 0U) return false;
    }
    return true;
}

bool range_is_supervisor_exact(std::uintptr_t virtual_address,
                               std::uintptr_t physical_address) {
    const auto first = first_index(virtual_address);
    for (std::uint32_t page = 0; page < workspace_pages; ++page) {
        const std::uint32_t expected =
            static_cast<std::uint32_t>(physical_address +
                                       page * paging::page_size) |
            paging::present | paging::writable;
        const std::uint32_t actual = supervisor_table[first + page];
        if (actual != expected || (actual & paging::user) != 0U) return false;
    }
    return true;
}

bool directory_is_supervisor_exact() {
    const std::uint32_t expected = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(supervisor_table)) |
        paging::present | paging::writable;
    const std::uint32_t actual = paging::page_directory[directory_index];
    return actual == expected && (actual & paging::user) == 0U;
}

void reset_state() {
    std::memset(paging::page_directory, 0, sizeof(paging::page_directory));
    std::memset(supervisor_table, 0, sizeof(supervisor_table));
    paging::enabled = true;
    paging::reload_count = 0U;
    zmid_content_workspace_allocator::allocations = 0U;
    zmid_content_workspace_allocator::mapping_active = false;
    zmid_content_workspace_allocator::mapping_fault = false;
    std::memset(reinterpret_cast<void*>(
                    supervisor_layout::zmid_workspace_virtual_a),
                0xA5, supervisor_layout::zmid_workspace_bytes * 2U);
}

int run() {
    if (!map_host_workspace()) return 10;
    reset_state();

    if (!zmid_content_workspace_allocator::acquire_mapping()) return 11;
    if (!directory_is_supervisor_exact() ||
        !range_is_supervisor_exact(
            supervisor_layout::zmid_workspace_virtual_a,
            supervisor_layout::zmid_workspace_physical_a) ||
        !range_is_supervisor_exact(
            supervisor_layout::zmid_workspace_virtual_b,
            supervisor_layout::zmid_workspace_physical_b) ||
        paging::reload_count != 1U) return 12;

    if (!zmid_content_workspace_allocator::acquire_mapping() ||
        paging::reload_count != 1U) return 13;
    const auto a_first = first_index(
        supervisor_layout::zmid_workspace_virtual_a);
    supervisor_table[a_first + 3U] |= paging::user;
    if (zmid_content_workspace_allocator::acquire_mapping()) return 14;
    supervisor_table[a_first + 3U] &= ~paging::user;

    zmid_content_workspace_allocator::release_mapping();
    if (!range_is_zero(supervisor_layout::zmid_workspace_virtual_a) ||
        !range_is_zero(supervisor_layout::zmid_workspace_virtual_b) ||
        paging::page_directory[directory_index] != 0U ||
        paging::reload_count != 2U) return 15;

    reset_state();
    paging::page_directory[directory_index] = 0x00123007U;
    if (zmid_content_workspace_allocator::acquire_mapping()) return 16;
    if (!range_is_zero(supervisor_layout::zmid_workspace_virtual_a) ||
        !range_is_zero(supervisor_layout::zmid_workspace_virtual_b) ||
        paging::reload_count != 0U ||
        paging::page_directory[directory_index] != 0x00123007U) return 17;

    reset_state();
    const auto b_first = first_index(
        supervisor_layout::zmid_workspace_virtual_b);
    supervisor_table[b_first + 7U] = 0x00123007U;
    if (zmid_content_workspace_allocator::acquire_mapping()) return 18;
    if (paging::page_directory[directory_index] != 0U ||
        supervisor_table[b_first + 7U] != 0x00123007U ||
        paging::reload_count != 0U) return 19;

    reset_state();
    void* a = zmid_content_workspace_allocator::allocate(
        supervisor_layout::zmid_workspace_bytes, 4096U);
    void* b = zmid_content_workspace_allocator::allocate(
        supervisor_layout::zmid_workspace_bytes, 16U);
    if (a != reinterpret_cast<void*>(
                 supervisor_layout::zmid_workspace_virtual_a) ||
        b != reinterpret_cast<void*>(
                 supervisor_layout::zmid_workspace_virtual_b)) return 20;
    if (zmid_content_workspace_allocator::allocate(
            supervisor_layout::zmid_workspace_bytes, 16U) != nullptr ||
        zmid_content_workspace_allocator::allocate(4096U, 16U) != nullptr ||
        zmid_content_workspace_allocator::allocate(
            supervisor_layout::zmid_workspace_bytes, 3U) != nullptr) return 21;
    const auto* a_bytes = static_cast<const std::uint8_t*>(a);
    const auto* b_bytes = static_cast<const std::uint8_t*>(b);
    for (std::uint32_t i = 0;
         i < supervisor_layout::zmid_workspace_bytes; ++i) {
        if (a_bytes[i] != 0U || b_bytes[i] != 0U) return 22;
    }
    zmid_content_workspace_allocator::release_mapping();
    if (!range_is_zero(supervisor_layout::zmid_workspace_virtual_a) ||
        !range_is_zero(supervisor_layout::zmid_workspace_virtual_b) ||
        paging::page_directory[directory_index] != 0U) return 23;

    reset_state();
    paging::enabled = false;
    if (zmid_content_workspace_allocator::allocate(
            supervisor_layout::zmid_workspace_bytes, 4096U) != nullptr ||
        paging::reload_count != 0U ||
        paging::page_directory[directory_index] != 0U) return 24;

    reset_state();
    if (!zmid_content_workspace_allocator::acquire_mapping()) return 25;
    paging::page_directory[directory_index] |= paging::user;
    zmid_content_workspace_allocator::release_mapping();
    if (!zmid_content_workspace_allocator::mapping_fault ||
        !zmid_content_workspace_allocator::mapping_active ||
        zmid_content_workspace_allocator::acquire_mapping()) return 26;

    std::printf("ZMID_WORKSPACE_MAPPING_TEST_OK pages=%u pde=isolated supervisor-only=yes conflict=blocked active=verified release=cleared fault=latched allocations=bounded\n",
                workspace_pages * 2U);
    return 0;
}
}

int main() { return run(); }
