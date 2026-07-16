#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct ZexHeader {
    char magic[4];
    std::uint32_t version;
    std::uint32_t header_size;
    std::uint32_t image_size;
    std::uint32_t entry_offset;
    std::uint32_t bss_size;
    std::uint32_t stack_size;
    std::uint32_t checksum;
};
#pragma pack(pop)

static_assert(sizeof(ZexHeader) == 32);

std::uint32_t fnv1a(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > 512 * 1024) throw std::runtime_error("invalid application size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("cannot read input");
    return bytes;
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: zex-pack <flat-binary> <output.zex>\n";
            return 2;
        }
        const auto image = read_all(argv[1]);
        const ZexHeader header{{'Z', 'E', 'X', '1'}, 1u, sizeof(ZexHeader),
                               static_cast<std::uint32_t>(image.size()), 0u, 0u,
                               64u * 1024u, fnv1a(image)};
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output");
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        if (!output) throw std::runtime_error("cannot write output");
        std::cout << "zex-pack: OK image=" << image.size() << " checksum=" << header.checksum << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zex-pack: " << error.what() << "\n";
        return 1;
    }
}
