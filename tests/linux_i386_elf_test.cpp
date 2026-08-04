#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;

#include "../kernel/parts/linux_i386_elf.inc"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input.is_open()) return 2;
    std::vector<uint8_t> fixture((std::istreambuf_iterator<char>(input)), {});
    linux_i386_elf::Layout layout{};
    if (fixture.empty() || !linux_i386_elf::validate(fixture.data(), fixture.size(), 0xFC000U, 4096U, layout) ||
        layout.image_bias != 0x08048000U || layout.entry_offset >= 0xFC000U || layout.load_count != 1U) return 1;

    auto negative = fixture;
    auto* header = reinterpret_cast<linux_i386_elf::Header*>(negative.data());
    auto* segment = reinterpret_cast<linux_i386_elf::ProgramHeader*>(negative.data() + header->program_offset);
    segment->flags = 7U;
    if (linux_i386_elf::validate(negative.data(), negative.size(), 0xFC000U, 4096U, layout)) return 1;

    negative = fixture;
    header = reinterpret_cast<linux_i386_elf::Header*>(negative.data());
    header->machine = 62U;
    if (linux_i386_elf::validate(negative.data(), negative.size(), 0xFC000U, 4096U, layout)) return 1;

    negative = fixture;
    header = reinterpret_cast<linux_i386_elf::Header*>(negative.data());
    segment = reinterpret_cast<linux_i386_elf::ProgramHeader*>(negative.data() + header->program_offset);
    segment[1].type = linux_i386_elf::pt_dynamic;
    if (linux_i386_elf::validate(negative.data(), negative.size(), 0xFC000U, 4096U, layout)) return 1;

    negative = fixture;
    header = reinterpret_cast<linux_i386_elf::Header*>(negative.data());
    header->entry = 0x09000000U;
    if (linux_i386_elf::validate(negative.data(), negative.size(), 0xFC000U, 4096U, layout)) return 1;

    std::cout << "LINUX_I386_ELF_TEST_OK bias=0x08048000 negatives=wx,machine,dynamic,entry\n";
    return 0;
}
