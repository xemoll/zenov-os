#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty()) throw std::runtime_error("read failed");
    return data;
}
std::uint16_t get16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data.at(offset) | (data.at(offset + 1) << 8));
}
std::uint32_t get32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(get16(data, offset)) |
           (static_cast<std::uint32_t>(get16(data, offset + 2)) << 16);
}
std::string text(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t size) {
    return std::string(reinterpret_cast<const char*>(data.data() + offset), size);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) { std::cerr << "usage: image-verify <zenov-os.img>\n"; return 2; }
        const auto image = read_all(argv[1]);
        if (image.size() != 1474560) throw std::runtime_error("image size is not 1.44 MiB");
        if (image[510] != 0x55 || image[511] != 0xAA) throw std::runtime_error("boot signature missing");
        if (text(image, 3, 8) != "ZENOVOS ") throw std::runtime_error("OEM label mismatch");
        if (get16(image, 11) != 512) throw std::runtime_error("sector size mismatch");
        if (text(image, 54, 8) != "FAT12   ") throw std::runtime_error("filesystem marker mismatch");
        const std::size_t root = 19 * 512;
        if (text(image, root, 11) != "ZENOVOS    ") throw std::runtime_error("volume entry missing");
        const std::size_t entry = root + 32;
        if (text(image, entry, 11) != "KERNEL  BIN") throw std::runtime_error("KERNEL.BIN entry missing");
        const auto cluster = get16(image, entry + 26);
        const auto size = get32(image, entry + 28);
        if (cluster != 2) throw std::runtime_error("kernel first cluster is not 2");
        if (size == 0 || size > 61440) throw std::runtime_error("kernel size outside loader contract");
        std::cout << "image-verify: OK image=1474560 boot=0xAA55 kernel=" << size
                  << " cluster=" << cluster << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "image-verify: error: " << error.what() << "\n";
        return 1;
    }
}
