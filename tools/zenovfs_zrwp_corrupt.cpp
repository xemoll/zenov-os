#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512U, kEntryCount = 128U, kDataStart = 32U, kSlotSectors = 128U;
#pragma pack(push, 1)
struct Entry { std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved; };
#pragma pack(pop)
static_assert(sizeof(Entry) == 64U);

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
}
std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input image");
    input.seekg(0, std::ios::end); const auto size = input.tellg(); input.seekg(0, std::ios::beg);
    if (size <= 0) throw std::runtime_error("empty input image");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read input image");
    return bytes;
}
void write_all(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output image");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write output image");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) { std::cerr << "usage: zenovfs-zrwp-corrupt <input.img> <output.img>\n"; return 2; }
        auto image = read_all(argv[1]);
        if (image.size() < kSectorSize + kEntryCount * sizeof(Entry)) throw std::runtime_error("truncated ZenovFS image");
        auto* entries = reinterpret_cast<Entry*>(image.data() + kSectorSize);
        Entry* target = nullptr;
        std::uint32_t index = 0;
        for (std::uint32_t i = 0; i < kEntryCount; ++i) {
            if (entries[i].used == 1U && entries[i].type == 1U && std::strcmp(entries[i].path, "/security/ransomware-policy.zrwp") == 0) {
                target = &entries[i]; index = i; break;
            }
        }
        if (!target || target->size < 128U) throw std::runtime_error("active ZRWP file missing or too small");
        const std::size_t offset = static_cast<std::size_t>(kDataStart + index * kSlotSectors) * kSectorSize;
        if (offset + target->size > image.size()) throw std::runtime_error("ZRWP slot outside image");
        image[offset + 120U] ^= 0x01U;
        target->checksum = fnv1a(image.data() + offset, target->size);
        write_all(argv[2], image);
        std::cout << "ZENOV_ZRWP_CORRUPT_IMAGE_OK path=/security/ransomware-policy.zrwp checksum-repaired=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-zrwp-corrupt: " << error.what() << "\n";
        return 1;
    }
}
