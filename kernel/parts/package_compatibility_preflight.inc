#ifndef ZENOV_PACKAGE_COMPATIBILITY_PREFLIGHT_INC
#define ZENOV_PACKAGE_COMPATIBILITY_PREFLIGHT_INC
namespace package_compatibility {

enum class Verdict : uint8_t { blocked, inspect_only, runnable_sandbox };

struct Result {
    package_foreign::Format format = package_foreign::Format::unknown;
    Verdict verdict = Verdict::blocked;
    const char* platform = "unknown";
    const char* reason = "format is not covered by an exact compatibility validator";
    bool recognized = false;
    bool structurally_valid = false;
    bool runtime_ready = false;
    bool trust_verified = false;
};

constexpr uint32_t pe_max_image_bytes = 512U * 1024U * 1024U;
constexpr uint32_t ps2_user_limit = 32U * 1024U * 1024U;
constexpr uint32_t xbe_max_image_bytes = 256U * 1024U * 1024U;

bool add_fits(uint32_t left, uint32_t right, uint32_t limit) {
    return left <= limit && right <= limit - left;
}

bool range_contains(uint32_t base, uint32_t span, uint32_t value) {
    return span && value >= base && value - base < span;
}

bool ranges_overlap(uint32_t left, uint32_t left_size, uint32_t right, uint32_t right_size) {
    if (!left_size || !right_size || left > 0xFFFFFFFFU - left_size ||
        right > 0xFFFFFFFFU - right_size) return true;
    return left < right + right_size && right < left + left_size;
}

uint16_t read_u16(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& valid) {
    return package_foreign::u16_le_at(bytes, size, offset, valid);
}

uint32_t read_u32(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& valid) {
    return package_foreign::u32_le_at(bytes, size, offset, valid);
}

bool validate_pe32(const uint8_t* bytes, uint32_t size) {
    if (!bytes || size < 0x138U || bytes[0] != 'M' || bytes[1] != 'Z') return false;
    bool valid = false;
    const uint32_t pe = read_u32(bytes, size, 0x3cU, valid);
    static const uint8_t signature[4] = {'P', 'E', 0U, 0U};
    if (!valid || pe < 0x40U || !package_foreign::bytes_at(bytes, size, pe, signature, 4U) ||
        !add_fits(pe, 24U, size)) return false;

    const uint16_t machine = read_u16(bytes, size, pe + 4U, valid);
    if (!valid || machine != 0x014cU) return false;
    const uint16_t sections = read_u16(bytes, size, pe + 6U, valid);
    if (!valid || !sections || sections > 96U) return false;
    const uint16_t optional_size = read_u16(bytes, size, pe + 20U, valid);
    if (!valid || optional_size < 96U) return false;
    const uint16_t characteristics = read_u16(bytes, size, pe + 22U, valid);
    if (!valid || !(characteristics & 0x0002U) || (characteristics & 0x2000U)) return false;

    const uint32_t optional = pe + 24U;
    if (!add_fits(optional, optional_size, size)) return false;
    const uint16_t magic = read_u16(bytes, size, optional, valid);
    if (!valid || magic != 0x010bU) return false;
    const uint32_t entry = read_u32(bytes, size, optional + 16U, valid);
    if (!valid || !entry) return false;
    const uint32_t section_alignment = read_u32(bytes, size, optional + 32U, valid);
    if (!valid || !section_alignment || (section_alignment & (section_alignment - 1U))) return false;
    const uint32_t file_alignment = read_u32(bytes, size, optional + 36U, valid);
    if (!valid || file_alignment < 512U || file_alignment > 65536U ||
        (file_alignment & (file_alignment - 1U)) || section_alignment < file_alignment) return false;
    const uint32_t image_size = read_u32(bytes, size, optional + 56U, valid);
    if (!valid || !image_size || image_size > pe_max_image_bytes || image_size % section_alignment) return false;
    const uint32_t headers_size = read_u32(bytes, size, optional + 60U, valid);
    if (!valid || !headers_size || headers_size > size || headers_size % file_alignment) return false;

    const uint32_t table = optional + optional_size;
    if (!add_fits(table, static_cast<uint32_t>(sections) * 40U, size) ||
        table + static_cast<uint32_t>(sections) * 40U > headers_size) return false;
    bool executable_entry = false;
    for (uint32_t i = 0U; i < sections; ++i) {
        const uint32_t at = table + i * 40U;
        const uint32_t virtual_size = read_u32(bytes, size, at + 8U, valid);
        if (!valid) return false;
        const uint32_t virtual_address = read_u32(bytes, size, at + 12U, valid);
        if (!valid) return false;
        const uint32_t raw_size = read_u32(bytes, size, at + 16U, valid);
        if (!valid) return false;
        const uint32_t raw_offset = read_u32(bytes, size, at + 20U, valid);
        if (!valid || (raw_size && (!add_fits(raw_offset, raw_size, size) || raw_offset < headers_size ||
            raw_offset % file_alignment || raw_size % file_alignment))) return false;
        const uint32_t flags = read_u32(bytes, size, at + 36U, valid);
        if (!valid || ((flags & 0xA0000000U) == 0xA0000000U)) return false;
        uint32_t mapped_size = virtual_size > raw_size ? virtual_size : raw_size;
        if (!mapped_size || virtual_address % section_alignment ||
            !add_fits(virtual_address, mapped_size, image_size)) return false;
        for (uint32_t j = 0U; j < i; ++j) {
            const uint32_t other = table + j * 40U;
            const uint32_t other_virtual_size = read_u32(bytes, size, other + 8U, valid);
            const uint32_t other_address = read_u32(bytes, size, other + 12U, valid);
            const uint32_t other_raw_size = read_u32(bytes, size, other + 16U, valid);
            if (!valid) return false;
            const uint32_t other_size = other_virtual_size > other_raw_size ? other_virtual_size : other_raw_size;
            if (ranges_overlap(virtual_address, mapped_size, other_address, other_size)) return false;
        }
        if ((flags & 0x20000000U) && range_contains(virtual_address, mapped_size, entry))
            executable_entry = true;
    }
    return executable_entry;
}

bool validate_ps2_elf(const uint8_t* bytes, uint32_t size) {
    if (!bytes || size < 52U || bytes[0] != 0x7fU || bytes[1] != 'E' || bytes[2] != 'L' ||
        bytes[3] != 'F' || bytes[4] != 1U || bytes[5] != 1U || bytes[6] != 1U) return false;
    bool valid = false;
    if (read_u16(bytes, size, 16U, valid) != 2U || !valid ||
        read_u16(bytes, size, 18U, valid) != 8U || !valid ||
        read_u32(bytes, size, 20U, valid) != 1U || !valid) return false;
    const uint32_t entry = read_u32(bytes, size, 24U, valid);
    const uint32_t phoff = read_u32(bytes, size, 28U, valid);
    const uint32_t flags = read_u32(bytes, size, 36U, valid);
    const uint16_t header_size = read_u16(bytes, size, 40U, valid);
    const uint16_t phsize = read_u16(bytes, size, 42U, valid);
    const uint16_t phcount = read_u16(bytes, size, 44U, valid);
    if (!valid || header_size != 52U || phsize != 32U || !phcount || phcount > 64U ||
        (flags & package_foreign::elf_mips_arch_mask) != package_foreign::elf_mips_arch_3 ||
        (flags & package_foreign::elf_mips_machine_mask) != package_foreign::elf_mips_machine_r5900 ||
        !add_fits(phoff, static_cast<uint32_t>(phcount) * 32U, size)) return false;
    bool executable_entry = false;
    for (uint32_t i = 0U; i < phcount; ++i) {
        const uint32_t at = phoff + i * 32U;
        const uint32_t type = read_u32(bytes, size, at, valid);
        if (!valid || type == 2U || type == 3U) return false;
        if (type != 1U) continue;
        const uint32_t offset = read_u32(bytes, size, at + 4U, valid);
        const uint32_t address = read_u32(bytes, size, at + 8U, valid);
        const uint32_t file_size = read_u32(bytes, size, at + 16U, valid);
        const uint32_t memory_size = read_u32(bytes, size, at + 20U, valid);
        const uint32_t segment_flags = read_u32(bytes, size, at + 24U, valid);
        const uint32_t alignment = read_u32(bytes, size, at + 28U, valid);
        if (!valid || !memory_size || file_size > memory_size || !add_fits(offset, file_size, size) ||
            address < 0x00010000U || !add_fits(address, memory_size, ps2_user_limit) ||
            (segment_flags & ~7U) || (segment_flags & 3U) == 3U ||
            (alignment > 1U && ((alignment & (alignment - 1U)) ||
             (address & (alignment - 1U)) != (offset & (alignment - 1U))))) return false;
        for (uint32_t j = 0U; j < i; ++j) {
            const uint32_t other = phoff + j * 32U;
            if (read_u32(bytes, size, other, valid) != 1U || !valid) continue;
            const uint32_t other_address = read_u32(bytes, size, other + 8U, valid);
            const uint32_t other_size = read_u32(bytes, size, other + 20U, valid);
            if (!valid || ranges_overlap(address, memory_size, other_address, other_size)) return false;
        }
        if ((segment_flags & 1U) && range_contains(address, memory_size, entry)) executable_entry = true;
    }
    return executable_entry;
}

bool validate_xbe(const uint8_t* bytes, uint32_t size) {
    static const uint8_t magic[4] = {'X','B','E','H'};
    if (!bytes || size < 0x178U || !package_foreign::bytes_at(bytes, size, 0U, magic, 4U)) return false;
    bool valid = false;
    const uint32_t base = read_u32(bytes, size, 0x104U, valid);
    const uint32_t headers_size = read_u32(bytes, size, 0x108U, valid);
    const uint32_t image_size = read_u32(bytes, size, 0x10cU, valid);
    const uint32_t image_header_size = read_u32(bytes, size, 0x110U, valid);
    const uint32_t certificate = read_u32(bytes, size, 0x118U, valid);
    const uint32_t sections = read_u32(bytes, size, 0x11cU, valid);
    const uint32_t section_table_address = read_u32(bytes, size, 0x120U, valid);
    if (!valid || base < 0x00010000U || base > 0x7fff0000U || headers_size < 0x178U ||
        headers_size > size || image_header_size < 0x178U || image_header_size > headers_size ||
        image_size < headers_size || image_size > xbe_max_image_bytes || !sections || sections > 64U ||
        certificate < base || certificate - base >= headers_size ||
        section_table_address < base || section_table_address - base >= headers_size) return false;
    const uint32_t table = section_table_address - base;
    if (!add_fits(table, sections * 56U, headers_size)) return false;
    for (uint32_t i = 0U; i < sections; ++i) {
        const uint32_t at = table + i * 56U;
        const uint32_t section_flags = read_u32(bytes, size, at, valid);
        const uint32_t virtual_address = read_u32(bytes, size, at + 4U, valid);
        const uint32_t virtual_size = read_u32(bytes, size, at + 8U, valid);
        const uint32_t raw_address = read_u32(bytes, size, at + 12U, valid);
        const uint32_t raw_size = read_u32(bytes, size, at + 16U, valid);
        if (!valid || !virtual_size || virtual_address < base ||
            !add_fits(virtual_address - base, virtual_size, image_size) ||
            (raw_size && (!add_fits(raw_address, raw_size, size) || raw_address < headers_size)) ||
            ((section_flags & 0x5U) == 0x5U)) return false;
        for (uint32_t j = 0U; j < i; ++j) {
            const uint32_t other = table + j * 56U;
            const uint32_t other_address = read_u32(bytes, size, other + 4U, valid);
            const uint32_t other_size = read_u32(bytes, size, other + 8U, valid);
            if (!valid || ranges_overlap(virtual_address, virtual_size, other_address, other_size)) return false;
        }
    }
    return true;
}

Result check(const uint8_t* bytes, uint32_t size, const char* path) {
    const package_foreign::Detection detected = package_foreign::classify(bytes, size, path);
    Result result{};
    result.format = detected.format;
    switch (detected.format) {
        case package_foreign::Format::elf: {
            linux_i386_elf::Layout layout{};
            result.platform = "Linux/i386";
            result.recognized = true;
            result.structurally_valid = linux_i386_elf::validate(bytes, size, 0x000fc000U, 4096U, layout);
            result.runtime_ready = result.structurally_valid;
            result.verdict = result.structurally_valid ? Verdict::runnable_sandbox : Verdict::blocked;
            result.reason = result.structurally_valid
                ? "static ET_EXEC fits the one-MiB ring-3 window; minimal write/exit ABI is available"
                : "malformed, dynamic, overlapping, W+X or out-of-window Linux/i386 ELF";
            return result;
        }
        case package_foreign::Format::portable_executable:
            result.platform = "Windows/i386";
            result.recognized = true;
            result.structurally_valid = validate_pe32(bytes, size);
            result.reason = result.structurally_valid
                ? "bounded PE32 layout passed; Authenticode and Win32 runtime are not yet available"
                : "malformed, overlapping, W+X, DLL, non-i386 or invalid-entry PE32";
            break;
        case package_foreign::Format::playstation_psx_exe:
            result.platform = "PlayStation 1";
            result.recognized = true;
            {
                ::psx_exe::Layout layout{};
                result.structurally_valid = ::psx_exe::validate(bytes, size, layout);
            }
            result.runtime_ready = result.structurally_valid;
            result.verdict = result.structurally_valid ? Verdict::runnable_sandbox : Verdict::blocked;
            result.reason = result.structurally_valid
                ? "PS-X EXE fits isolated two-MiB RAM; bounded R3000A diagnostic runtime is available"
                : "malformed or out-of-range PS-X EXE header and payload";
            return result;
        case package_foreign::Format::playstation_ps2_elf:
            result.platform = "PlayStation 2";
            result.recognized = true;
            result.structurally_valid = validate_ps2_elf(bytes, size);
            result.reason = result.structurally_valid
                ? "bounded R5900 ET_EXEC layout passed; Emotion Engine runtime is not yet available"
                : "malformed, dynamic, overlapping, W+X, generic-MIPS or invalid-entry PS2 ELF";
            break;
        case package_foreign::Format::xbox_xbe:
            result.platform = "Original Xbox";
            result.recognized = true;
            result.structurally_valid = validate_xbe(bytes, size);
            result.reason = result.structurally_valid
                ? "bounded XBE header and sections passed; signature trust and Xbox runtime are not yet available"
                : "malformed, overlapping, W+X or out-of-image XBE layout";
            break;
        default:
            return result;
    }
    result.verdict = result.structurally_valid ? Verdict::inspect_only : Verdict::blocked;
    return result;
}

const char* verdict_text(Verdict verdict) {
    switch (verdict) {
        case Verdict::runnable_sandbox: return "runnable-sandbox";
        case Verdict::inspect_only: return "inspect-only";
        case Verdict::blocked: return "blocked";
    }
    return "blocked";
}

} // namespace package_compatibility
#endif
