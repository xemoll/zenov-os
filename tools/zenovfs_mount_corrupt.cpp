#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kSectorSize = 512U;
constexpr std::size_t kEntryCount = 128U;
#pragma pack(push, 1)
struct Superblock {
    char magic[8]; std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation;
    char label[16]; std::uint8_t reserved[460];
};
struct Entry { std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved; };
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize && sizeof(Entry) == 64U);

std::vector<std::uint8_t> read_image(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input image");
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < static_cast<std::streamoff>(kSectorSize + kEntryCount * sizeof(Entry))) throw std::runtime_error("input image is too small");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("cannot read input image");
    return bytes;
}

void write_image(const char* path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output image");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write output image");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: zenovfs-mount-corrupt <source.img> <mode> <output.img>\n";
            return 2;
        }
        auto image = read_image(argv[1]);
        auto& super = *reinterpret_cast<Superblock*>(image.data());
        auto* entries = reinterpret_cast<Entry*>(image.data() + kSectorSize);
        const std::string mode = argv[2];
        std::size_t sample = kEntryCount;
        for (std::size_t i = 0U; i < kEntryCount; ++i) {
            if (entries[i].used && entries[i].type == 1U) { sample = i; break; }
        }
        if (sample == kEntryCount) throw std::runtime_error("no file entry for corruption fixture");
        std::size_t spare = kEntryCount;
        for (std::size_t i = kEntryCount; i > 0U; --i) {
            if (!entries[i - 1U].used) { spare = i - 1U; break; }
        }

        if (mode == "entry-sectors") {
            super.entry_sectors = 17U;        } else if (mode == "metadata-layout") {
            super.data_start = 16U;
        } else if (mode == "slot-size") {
            super.slot_sectors = 129U;
        } else if (mode == "entry-path") {
            std::memset(entries[sample].path, 'A', sizeof(entries[sample].path));
            entries[sample].path[0] = '/';
        } else if (mode == "component-depth") {
            static constexpr char deep[] = "/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a";
            std::memset(entries[sample].path, 0, sizeof(entries[sample].path));
            std::memcpy(entries[sample].path, deep, sizeof(deep));
        } else if (mode == "duplicate-path") {
            if (spare == kEntryCount) throw std::runtime_error("no free entry for duplicate fixture");
            entries[spare] = entries[sample];        } else if (mode == "transition-old-mismatch") {
            if (spare == kEntryCount) throw std::runtime_error("no free entry for recovery fixture");
            entries[spare] = entries[sample];
            entries[spare].flags = 1U;
            entries[spare].reserved = 0U;
        } else {
            throw std::runtime_error("unknown corruption mode");
        }
        write_image(argv[3], image);
        std::cout << "ZENOVFS_MOUNT_CORRUPT_OK mode=" << mode << " output=" << argv[3] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-mount-corrupt: " << error.what() << '\n';
        return 1;
    }
}
