namespace process {
bool elf_permissions_allowed(const uint8_t* file, uint32_t file_size) {
    if (!file || file_size < sizeof(Elf32Header)) return true;
    const auto* header = reinterpret_cast<const Elf32Header*>(file);
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F') return true;
    if (header->program_entry_size != sizeof(Elf32ProgramHeader) ||
        header->program_offset > file_size ||
        header->program_count > (file_size - header->program_offset) / sizeof(Elf32ProgramHeader)) return true;

    for (uint32_t i = 0; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const Elf32ProgramHeader*>(
            file + header->program_offset + i * sizeof(Elf32ProgramHeader));
        if (segment->type == 1U && (segment->flags & 0x3U) == 0x3U) return false;
    }
    return true;
}

bool elf_policy_self_test() {
    struct Sample {
        Elf32Header header;
        Elf32ProgramHeader segment;
    } sample{};
    sample.header.ident[0] = 0x7FU;
    sample.header.ident[1] = 'E';
    sample.header.ident[2] = 'L';
    sample.header.ident[3] = 'F';
    sample.header.program_offset = sizeof(Elf32Header);
    sample.header.program_entry_size = sizeof(Elf32ProgramHeader);
    sample.header.program_count = 1;
    sample.segment.type = 1;
    sample.segment.flags = 7;
    if (elf_permissions_allowed(reinterpret_cast<const uint8_t*>(&sample), sizeof(sample))) return false;
    sample.segment.flags = 5;
    return elf_permissions_allowed(reinterpret_cast<const uint8_t*>(&sample), sizeof(sample));
}

bool run(char* input) {
    return input && run_unchecked(input);
}
} // namespace process
