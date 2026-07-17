#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kSectorSize = 512;
constexpr std::size_t kFatSectors = 9;
constexpr std::size_t kRootStartSector = 19;
constexpr std::size_t kDataStartSector = 33;
constexpr std::uint32_t kKernelMaximum = 512U * 1024U;

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path);
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
std::uint16_t fat12_get(const std::vector<std::uint8_t>& image, std::size_t fat_offset, std::uint16_t cluster) {
    const std::size_t offset = fat_offset + cluster + cluster / 2U;
    const std::uint16_t pair = get16(image, offset);
    return static_cast<std::uint16_t>((cluster & 1U) ? (pair >> 4U) : (pair & 0x0FFFU));
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) { std::cerr << "usage: image-verify <zenov-os.img>\n"; return 2; }
        const auto image = read_all(argv[1]);
        if (image.size() != 1474560) throw std::runtime_error("image size is not 1.44 MiB");
        if (image[510] != 0x55 || image[511] != 0xAA) throw std::runtime_error("boot signature missing");
        if (text(image, 3, 8) != "ZENOVOS ") throw std::runtime_error("OEM label mismatch");
        if (get16(image, 11) != kSectorSize) throw std::runtime_error("sector size mismatch");
        if (text(image, 54, 8) != "FAT12   ") throw std::runtime_error("filesystem marker mismatch");

        const std::size_t fat1 = kSectorSize;
        const std::size_t fat2 = fat1 + kFatSectors * kSectorSize;
        for (std::size_t index = 0; index < kFatSectors * kSectorSize; ++index) {
            if (image[fat1 + index] != image[fat2 + index]) throw std::runtime_error("FAT copies differ");
        }

        const std::size_t root = kRootStartSector * kSectorSize;
        if (text(image, root, 11) != "ZENOVOS    ") throw std::runtime_error("volume entry missing");
        const std::size_t entry = root + 32;
        if (text(image, entry, 11) != "KERNEL  BIN") throw std::runtime_error("KERNEL.BIN entry missing");
        const auto first_cluster = get16(image, entry + 26);
        const auto size = get32(image, entry + 28);
        if (first_cluster != 2) throw std::runtime_error("kernel first cluster is not 2");
        if (size == 0 || size > kKernelMaximum) throw std::runtime_error("kernel size outside segmented-loader contract");

        const std::uint32_t expected_clusters = (size + kSectorSize - 1U) / kSectorSize;
        std::vector<bool> visited(4096, false);
        std::uint16_t cluster = first_cluster;
        std::uint32_t chain_length = 0;
        while (cluster < 0x0FF0U) {
            if (cluster < 2U || cluster >= visited.size() || visited[cluster]) throw std::runtime_error("invalid or cyclic kernel FAT chain");
            visited[cluster] = true;
            ++chain_length;
            if (chain_length > expected_clusters) throw std::runtime_error("kernel FAT chain is longer than file size");
            const std::size_t sector = kDataStartSector + static_cast<std::size_t>(cluster - 2U);
            if ((sector + 1U) * kSectorSize > image.size()) throw std::runtime_error("kernel cluster is outside image");
            cluster = fat12_get(image, fat1, cluster);
        }
        if (cluster < 0x0FF8U || chain_length != expected_clusters) throw std::runtime_error("kernel FAT chain does not cover the file");

        std::cout << "image-verify: OK image=1474560 boot=0xAA55 kernel=" << size
                  << " clusters=" << chain_length << " first=" << first_cluster
                  << " loader=segmented\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "image-verify: error: " << error.what() << "\n";
        return 1;
    }
}
