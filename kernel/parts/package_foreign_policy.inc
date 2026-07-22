namespace package_foreign {

inline constexpr uint32_t elf_mips_arch_mask = 0xf0000000U;
inline constexpr uint32_t elf_mips_arch_3 = 0x20000000U;
inline constexpr uint32_t elf_mips_machine_mask = 0x00ff0000U;
inline constexpr uint32_t elf_mips_machine_r5900 = 0x00920000U;

inline Detection classify(const uint8_t* bytes, uint32_t size, const char* path) {
    static const uint8_t elf_magic[4] = {0x7fU, 'E', 'L', 'F'};
    if (!bytes_at(bytes, size, 0U, elf_magic, sizeof(elf_magic)) ||
        suffix_folded(path, ".appimage") || size < 20U) {
        return detect(bytes, size, path);
    }

    const uint8_t elf_class = bytes[4];
    const uint8_t elf_data = bytes[5];
    bool machine_valid = false;
    const uint16_t machine = elf_data == 1U
        ? u16_le_at(bytes, size, 18U, machine_valid)
        : u16_be_at(bytes, size, 18U, machine_valid);
    if (!machine_valid || elf_class != 1U || machine != 8U) {
        return detect(bytes, size, path);
    }

    if (elf_data == 1U && size >= 40U) {
        bool flags_valid = false;
        const uint32_t flags = u32_le_at(bytes, size, 36U, flags_valid);
        if (flags_valid &&
            (flags & elf_mips_arch_mask) == elf_mips_arch_3 &&
            (flags & elf_mips_machine_mask) == elf_mips_machine_r5900) {
            return result(Format::playstation_ps2_elf, Support::runtime_required,
                          "PlayStation 2 R5900 ELF", "PlayStation 2",
                          "R5900 ELF flags are recognized; Emotion Engine/IOP runtime and console services are absent");
        }
    }

    return result(Format::elf_foreign, Support::runtime_required,
                  "foreign MIPS ELF executable", "Linux/Unix/MIPS",
                  "generic MIPS ELF is not identified as PlayStation 2 without R5900 architecture flags");
}

} // namespace package_foreign
