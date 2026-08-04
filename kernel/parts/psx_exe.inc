#ifndef ZENOV_PSX_EXE_INC
#define ZENOV_PSX_EXE_INC
namespace psx_exe {

constexpr uint32_t header_bytes = 0x800U;
constexpr uint32_t ram_bytes = 2U * 1024U * 1024U;

struct Layout {
    uint32_t entry = 0U;
    uint32_t global_pointer = 0U;
    uint32_t load_address = 0U;
    uint32_t payload_bytes = 0U;
    uint32_t fill_address = 0U;
    uint32_t fill_bytes = 0U;
    uint32_t stack_pointer = 0U;
};

uint32_t read_u32(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& okay) {
    if (!bytes || offset > size || size - offset < 4U) { okay = false; return 0U; }
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
}

bool address_offset(uint32_t address, uint32_t& offset) {
    if (address < ram_bytes) { offset = address; return true; }
    if (address >= 0x80000000U && address < 0x80000000U + ram_bytes) {
        offset = address - 0x80000000U;
        return true;
    }
    if (address >= 0xA0000000U && address < 0xA0000000U + ram_bytes) {
        offset = address - 0xA0000000U;
        return true;
    }
    return false;
}

bool bounded_range(uint32_t address, uint32_t bytes, uint32_t& offset) {
    return bytes && address_offset(address, offset) && bytes <= ram_bytes - offset;
}

bool ranges_overlap(uint32_t left, uint32_t left_size, uint32_t right, uint32_t right_size) {
    return left_size && right_size && left < right + right_size && right < left + left_size;
}

bool validate(const uint8_t* bytes, uint32_t size, Layout& output) {
    static const uint8_t magic[8] = {'P','S','-','X',' ','E','X','E'};
    output = Layout{};
    if (!bytes || size < header_bytes) return false;
    for (uint32_t i = 0U; i < 8U; ++i) if (bytes[i] != magic[i]) return false;

    bool okay = true;
    Layout candidate{};
    candidate.entry = read_u32(bytes, size, 0x10U, okay);
    candidate.global_pointer = read_u32(bytes, size, 0x14U, okay);
    candidate.load_address = read_u32(bytes, size, 0x18U, okay);
    candidate.payload_bytes = read_u32(bytes, size, 0x1cU, okay);
    candidate.fill_address = read_u32(bytes, size, 0x28U, okay);
    candidate.fill_bytes = read_u32(bytes, size, 0x2cU, okay);
    const uint32_t stack_start = read_u32(bytes, size, 0x30U, okay);
    const uint32_t stack_size = read_u32(bytes, size, 0x34U, okay);
    if (!okay || !candidate.payload_bytes || (candidate.payload_bytes & 3U) ||
        candidate.payload_bytes > size - header_bytes) return false;

    uint32_t load_offset = 0U, entry_offset = 0U;
    if (!bounded_range(candidate.load_address, candidate.payload_bytes, load_offset) ||
        !address_offset(candidate.entry, entry_offset) ||
        entry_offset < load_offset || entry_offset - load_offset >= candidate.payload_bytes ||
        (candidate.entry & 3U)) return false;

    if (candidate.fill_bytes) {
        uint32_t fill_offset = 0U;
        if (!bounded_range(candidate.fill_address, candidate.fill_bytes, fill_offset) ||
            ranges_overlap(load_offset, candidate.payload_bytes, fill_offset, candidate.fill_bytes)) return false;
    } else if (candidate.fill_address) {
        return false;
    }

    if (stack_start || stack_size) {
        uint32_t stack_offset = 0U;
        if (!address_offset(stack_start, stack_offset) || stack_size > ram_bytes - stack_offset ||
            (stack_start & 3U)) return false;
        candidate.stack_pointer = stack_start + stack_size;
        uint32_t ignored = 0U;
        if (!address_offset(candidate.stack_pointer - (stack_size ? 1U : 0U), ignored)) return false;
    } else {
        candidate.stack_pointer = 0x801FFF00U;
    }

    output = candidate;
    return true;
}

} // namespace psx_exe
#endif
