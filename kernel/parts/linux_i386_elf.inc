#ifndef ZENOV_LINUX_I386_ELF_INC
#define ZENOV_LINUX_I386_ELF_INC
namespace linux_i386_elf {
constexpr uint32_t pt_load = 1U;
constexpr uint32_t pt_dynamic = 2U;
constexpr uint32_t pt_interp = 3U;

struct [[gnu::packed]] Header {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version, entry, program_offset, section_offset, flags;
    uint16_t header_size, program_entry_size, program_count, section_entry_size, section_count, string_section;
};
struct [[gnu::packed]] ProgramHeader {
    uint32_t type, offset, virtual_address, physical_address, file_size, memory_size, flags, alignment;
};
static_assert(sizeof(Header) == 52U && sizeof(ProgramHeader) == 32U);

struct Layout { uint32_t image_bias = 0U, entry_offset = 0U, load_count = 0U; };

constexpr bool overlaps(uint32_t left, uint32_t left_size, uint32_t right, uint32_t right_size) {
    return left < right + right_size && right < left + left_size;
}

bool validate(const uint8_t* file, uint32_t file_size, uint32_t window_limit,
              uint32_t page_size, Layout& layout) {
    layout = {};
    if (!file || file_size < sizeof(Header) || !window_limit || !page_size ||
        (page_size & (page_size - 1U))) return false;
    const auto* header = reinterpret_cast<const Header*>(file);
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' || header->ident[2] != 'L' ||
        header->ident[3] != 'F' || header->ident[4] != 1U || header->ident[5] != 1U ||
        header->ident[6] != 1U || (header->ident[7] != 0U && header->ident[7] != 3U) ||
        header->ident[8] != 0U || header->type != 2U || header->machine != 3U ||
        header->version != 1U || header->header_size != sizeof(Header) ||
        header->program_entry_size != sizeof(ProgramHeader) || header->program_offset > file_size ||
        !header->program_count ||
        header->program_count > (file_size - header->program_offset) / sizeof(ProgramHeader)) return false;

    uint32_t lowest = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const ProgramHeader*>(
            file + header->program_offset + i * sizeof(ProgramHeader));
        if (segment->type == pt_interp || segment->type == pt_dynamic) return false;
        if (segment->type != pt_load) continue;
        if (!segment->memory_size) {
            if (segment->file_size) return false;
            continue;
        }
        if (segment->file_size > segment->memory_size || segment->offset > file_size ||
            segment->file_size > file_size - segment->offset || (segment->flags & ~7U) ||
            (segment->flags & 3U) == 3U ||
            (segment->alignment > 1U && ((segment->alignment & (segment->alignment - 1U)) ||
             (segment->virtual_address & (segment->alignment - 1U)) !=
             (segment->offset & (segment->alignment - 1U))))) return false;
        if (segment->virtual_address < lowest) lowest = segment->virtual_address;
        ++layout.load_count;
    }
    if (!layout.load_count || lowest == 0U || lowest == 0xFFFFFFFFU) return false;
    layout.image_bias = lowest & ~(page_size - 1U);
    if (layout.image_bias > 0xFFFFFFFFU - window_limit || header->entry < layout.image_bias) return false;
    layout.entry_offset = header->entry - layout.image_bias;
    if (layout.entry_offset >= window_limit) return false;

    bool executable_entry = false;
    for (uint32_t i = 0U; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const ProgramHeader*>(
            file + header->program_offset + i * sizeof(ProgramHeader));
        if (segment->type != pt_load || !segment->memory_size ||
            segment->virtual_address < layout.image_bias) continue;
        const uint32_t relative = segment->virtual_address - layout.image_bias;
        if (relative >= window_limit || segment->memory_size > window_limit - relative) return false;
        for (uint32_t j = 0U; j < i; ++j) {
            const auto* other = reinterpret_cast<const ProgramHeader*>(
                file + header->program_offset + j * sizeof(ProgramHeader));
            if (other->type != pt_load || !other->memory_size ||
                other->virtual_address < layout.image_bias) continue;
            const uint32_t other_relative = other->virtual_address - layout.image_bias;
            if (overlaps(relative, segment->memory_size, other_relative, other->memory_size)) return false;
            const uint32_t left_page = relative & ~(page_size - 1U);
            const uint32_t right_page = other_relative & ~(page_size - 1U);
            const uint32_t left_end = (relative + segment->memory_size + page_size - 1U) & ~(page_size - 1U);
            const uint32_t right_end = (other_relative + other->memory_size + page_size - 1U) & ~(page_size - 1U);
            if (((segment->flags ^ other->flags) & 2U) && left_page < right_end && right_page < left_end)
                return false;
        }
        if ((segment->flags & 1U) && header->entry >= segment->virtual_address &&
            header->entry - segment->virtual_address < segment->memory_size) executable_entry = true;
    }
    return executable_entry;
}
} // namespace linux_i386_elf
#endif
