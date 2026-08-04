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
std::uint32_t user_table[1024]{};
bool enabled = true;
std::uint32_t reload_count = 0U;
void reload() { ++reload_count; }
}

#include "../kernel/parts/zmid_content_workspace.inc"

namespace {
using zmid_content_workspace_allocator::workspace_pages;

bool map_host_workspace() {
    const std::size_t bytes = supervisor_layout::zmid_workspace_bytes * 2U;
    void* const wanted = reinterpret_cast<void*>(supervisor_layout::zmid_workspace_a);
    void* mapped = mmap(wanted, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return mapped == wanted;
}

std::uint32_t first_index(std::uintptr_t address) {
    return static_cast<std::uint32_t>((address - paging::table_span) / paging::page_size);
}

bool range_is_zero(std::uintptr_t address) {
    const auto first = first_index(address);
    for (std::uint32_t page = 0; page < workspace_pages; ++page) {
        if (paging::user_table[first + page] != 0U) return false;
    }
    return true;
}

bool range_is_supervisor_exact(std::uintptr_t address) {
    const auto first = first_index(address);
    for (std::uint32_t page = 0; page < workspace_pages; ++page) {
        const std::uint32_t expected = static_cast<std::uint32_t>(address + page * paging::page_size) |
                                       paging::present | paging::writable;
        const std::uint32_t actual = paging::user_table[first + page];
        if (actual != expected || (actual & paging::user) != 0U) return false;
    }
    return true;
}

void reset_state() {
    std::memset(paging::user_table, 0, sizeof(paging::user_table));
    paging::enabled = true;
    paging::reload_count = 0U;
    zmid_content_workspace_allocator::allocations = 0U;
    zmid_content_workspace_allocator::mapping_active = false;
    std::memset(reinterpret_cast<void*>(supervisor_layout::zmid_workspace_a), 0xA5,
                supervisor_layout::zmid_workspace_bytes * 2U);
}

int run() {
    if (!map_host_workspace()) return 10;
    reset_state();

    if (!zmid_content_workspace_allocator::acquire_mapping()) return 11;
    if (!range_is_supervisor_exact(supervisor_layout::zmid_workspace_a) ||
        !range_is_supervisor_exact(supervisor_layout::zmid_workspace_b) ||
        paging::reload_count != 1U) return 12;

    if (!zmid_content_workspace_allocator::acquire_mapping() || paging::reload_count != 1U) return 13;
    const auto a_first = first_index(supervisor_layout::zmid_workspace_a);
    paging::user_table[a_first + 3U] |= paging::user;
    if (zmid_content_workspace_allocator::acquire_mapping()) return 14;
    paging::user_table[a_first + 3U] &= ~paging::user;

    zmid_content_workspace_allocator::release_mapping();
    if (!range_is_zero(supervisor_layout::zmid_workspace_a) ||
        !range_is_zero(supervisor_layout::zmid_workspace_b) ||
        paging::reload_count != 2U) return 15;

    reset_state();
    const auto b_first = first_index(supervisor_layout::zmid_workspace_b);
    paging::user_table[b_first + 7U] = 0x00123007U;
    if (zmid_content_workspace_allocator::acquire_mapping()) return 16;
    if (!range_is_zero(supervisor_layout::zmid_workspace_a) || paging::reload_count != 0U) return 17;
    if (paging::user_table[b_first + 7U] != 0x00123007U) return 18;

    reset_state();
    void* a = zmid_content_workspace_allocator::allocate(supervisor_layout::zmid_workspace_bytes, 4096U);
    void* b = zmid_content_workspace_allocator::allocate(supervisor_layout::zmid_workspace_bytes, 16U);
    if (a != reinterpret_cast<void*>(supervisor_layout::zmid_workspace_a) ||
        b != reinterpret_cast<void*>(supervisor_layout::zmid_workspace_b)) return 19;
    if (zmid_content_workspace_allocator::allocate(supervisor_layout::zmid_workspace_bytes, 16U) != nullptr ||
        zmid_content_workspace_allocator::allocate(4096U, 16U) != nullptr ||
        zmid_content_workspace_allocator::allocate(supervisor_layout::zmid_workspace_bytes, 3U) != nullptr) return 20;
    const auto* a_bytes = static_cast<const std::uint8_t*>(a);
    const auto* b_bytes = static_cast<const std::uint8_t*>(b);
    for (std::uint32_t i = 0; i < supervisor_layout::zmid_workspace_bytes; ++i) {
        if (a_bytes[i] != 0U || b_bytes[i] != 0U) return 21;
    }
    zmid_content_workspace_allocator::release_mapping();
    if (!range_is_zero(supervisor_layout::zmid_workspace_a) ||
        !range_is_zero(supervisor_layout::zmid_workspace_b)) return 22;

    reset_state();
    paging::enabled = false;
    void* pre_paging = zmid_content_workspace_allocator::allocate(
        supervisor_layout::zmid_workspace_bytes, 4096U);
    if (pre_paging != reinterpret_cast<void*>(supervisor_layout::zmid_workspace_a) ||
        paging::reload_count != 0U || !range_is_zero(supervisor_layout::zmid_workspace_a) ||
        !range_is_zero(supervisor_layout::zmid_workspace_b)) return 23;

    std::printf("ZMID_WORKSPACE_MAPPING_TEST_OK pages=%u supervisor-only=yes conflict=blocked active=verified release=cleared allocations=bounded\n",
                workspace_pages * 2U);
    return 0;
}
}

int main() { return run(); }
