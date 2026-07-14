#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kSectorSize = 512;
constexpr std::size_t kTotalSectors = 2880;
constexpr std::size_t kFatSectors = 9;
constexpr std::size_t kFatCount = 2;
constexpr std::size_t kRootEntries = 224;
constexpr std::size_t kRootSectors = (kRootEntries * 32 + kSectorSize - 1) / kSectorSize;
constexpr std::size_t kRootStart = 1 + kFatCount * kFatSectors;
constexpr std::size_t kDataStart = kRootStart + kRootSectors;
constexpr std::size_t kImageSize = kSectorSize * kTotalSectors;

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path);
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty()) throw std::runtime_error("cannot read " + path);
    return data;
}

void write_all(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create " + path);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("cannot write " + path);
}

void put16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value);
    data.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    put16(data, offset, static_cast<std::uint16_t>(value));
    put16(data, offset + 2, static_cast<std::uint16_t>(value >> 16));
}
void fat12_set(std::vector<std::uint8_t>& fat, std::uint16_t cluster, std::uint16_t value) {
    const std::size_t offset = cluster + cluster / 2;
    std::uint16_t current = static_cast<std::uint16_t>(fat.at(offset) | (fat.at(offset + 1) << 8));
    if (cluster & 1) current = static_cast<std::uint16_t>((current & 0x000F) | ((value & 0x0FFF) << 4));
    else current = static_cast<std::uint16_t>((current & 0xF000) | (value & 0x0FFF));
    fat.at(offset) = static_cast<std::uint8_t>(current);
    fat.at(offset + 1) = static_cast<std::uint8_t>(current >> 8);
}
void copy_name(std::vector<std::uint8_t>& image, std::size_t offset, const char* text, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) image.at(offset + i) = static_cast<std::uint8_t>(text[i]);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: fat12-builder <BOOT.BIN> <KERNEL.BIN> <zenov-os.img>\n";
            return 2;
        }
        const auto boot = read_all(argv[1]);
        const auto kernel = read_all(argv[2]);
        if (boot.size() != kSectorSize) throw std::runtime_error("boot sector must be exactly 512 bytes");
        if (boot[510] != 0x55 || boot[511] != 0xAA) throw std::runtime_error("boot signature 0xAA55 missing");
        if (kernel.empty()) throw std::runtime_error("kernel is empty");
        const std::size_t clusters = (kernel.size() + kSectorSize - 1) / kSectorSize;
        if (clusters > kTotalSectors - kDataStart) throw std::runtime_error("kernel does not fit FAT12 image");

        std::vector<std::uint8_t> image(kImageSize, 0);
        for (std::size_t i = 0; i < boot.size(); ++i) image[i] = boot[i];
        std::vector<std::uint8_t> fat(kFatSectors * kSectorSize, 0);
        fat[0] = 0xF0; fat[1] = 0xFF; fat[2] = 0xFF;
        for (std::size_t i = 0; i < clusters; ++i) {
            const auto cluster = static_cast<std::uint16_t>(2 + i);
            fat12_set(fat, cluster, i + 1 == clusters ? 0x0FFF : static_cast<std::uint16_t>(cluster + 1));
        }
        const std::size_t fat1 = kSectorSize;
        const std::size_t fat2 = fat1 + fat.size();
        for (std::size_t i = 0; i < fat.size(); ++i) { image[fat1 + i] = fat[i]; image[fat2 + i] = fat[i]; }

        const std::size_t root = kRootStart * kSectorSize;
        copy_name(image, root, "ZENOVOS    ", 11);
        image[root + 11] = 0x08;
        const std::size_t entry = root + 32;
        copy_name(image, entry, "KERNEL  BIN", 11);
        image[entry + 11] = 0x20;
        put16(image, entry + 26, 2);
        put32(image, entry + 28, static_cast<std::uint32_t>(kernel.size()));

        const std::size_t data = kDataStart * kSectorSize;
        for (std::size_t i = 0; i < kernel.size(); ++i) image[data + i] = kernel[i];
        write_all(argv[3], image);
        std::cout << "fat12-builder: wrote " << argv[3] << " (kernel=" << kernel.size()
                  << " bytes, clusters=" << clusters << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fat12-builder: error: " << error.what() << "\n";
        return 1;
    }
}
