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
struct Superblock { char magic[8]; std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation; char label[16]; std::uint8_t reserved[460]; };
struct Entry { std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved; };
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize && sizeof(Entry) == 64U);

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input image");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("invalid image size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read image");
    return bytes;
}

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0U; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
}

Entry* find_entry(std::vector<std::uint8_t>& disk, const char* path, std::uint32_t& index) {
    if (disk.size() < kSectorSize + kEntryCount * sizeof(Entry) || std::memcmp(disk.data(), "ZENOVFS1", 8U) != 0) {
        throw std::runtime_error("not a ZenovFS1 image");
    }
    auto* entries = reinterpret_cast<Entry*>(disk.data() + kSectorSize);
    for (std::uint32_t i = 0U; i < kEntryCount; ++i) {
        if (entries[i].used && entries[i].type == 1U && std::strncmp(entries[i].path, path, sizeof(entries[i].path)) == 0) {
            index = i;
            return &entries[i];
        }
    }
    throw std::runtime_error(std::string("file not found: ") + path);
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
        if (argc != 4) {
            std::cerr << "usage: zenovfs-zvrt-corrupt <input.img> <output.img> <manifest|data>\n";
            return 2;
        }
        auto disk = read_all(argv[1]);
        const std::string mode = argv[3];
        const char* path = mode == "manifest" ? "/security/verified-reads.zvrt" :
                           mode == "data" ? "/samples/clean.txt" : nullptr;
        if (!path) throw std::runtime_error("unknown corruption mode");
        std::uint32_t index = 0U;
        Entry* entry = find_entry(disk, path, index);
        if (!entry->size) throw std::runtime_error("cannot corrupt empty file");
        const std::size_t offset = static_cast<std::size_t>(kDataStart + index * kSlotSectors) * kSectorSize;
        if (offset + entry->size > disk.size()) throw std::runtime_error("file slot exceeds image");
        const std::size_t mutation = mode == "manifest" ? 80U + 48U : 0U;
        if (mutation >= entry->size) throw std::runtime_error("corruption offset exceeds file");
        disk[offset + mutation] ^= 0x01U;
        entry->checksum = fnv1a(disk.data() + offset, entry->size);
        write_all(argv[2], disk);
        std::cout << "zenovfs-zvrt-corrupt: OK mode=" << mode << " path=" << path
                  << " entry=" << index << " checksum-repaired=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-zvrt-corrupt: " << error.what() << "\n";
        return 1;
    }
}
