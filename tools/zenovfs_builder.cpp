#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kTotalSectors = 32768;
constexpr std::uint32_t kEntryCount = 128;
constexpr std::uint32_t kEntrySectors = 16;
constexpr std::uint32_t kDataStart = 32;
constexpr std::uint32_t kSlotSectors = 128;
constexpr std::uint32_t kMaxFileBytes = kSlotSectors * kSectorSize;

#pragma pack(push, 1)
struct Superblock {
    char magic[8];
    std::uint32_t version;
    std::uint32_t total_sectors;
    std::uint32_t entry_count;
    std::uint32_t entry_sectors;
    std::uint32_t data_start;
    std::uint32_t slot_sectors;
    std::uint32_t generation;
    char label[16];
    std::uint8_t reserved[460];
};

struct Entry {
    std::uint8_t used;
    std::uint8_t type;
    std::uint16_t flags;
    char path[48];
    std::uint32_t size;
    std::uint32_t checksum;
    std::uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(Superblock) == kSectorSize);
static_assert(sizeof(Entry) == 64);

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open seed file: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(kMaxFileBytes)) {
        throw std::runtime_error("seed file exceeds ZenovFS slot capacity");
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input && !bytes.empty()) throw std::runtime_error("cannot read seed file");
    return bytes;
}

void set_path(Entry& entry, const std::string& path) {
    if (path.empty() || path.size() >= sizeof(entry.path) || path.front() != '/') {
        throw std::runtime_error("invalid ZenovFS path: " + path);
    }
    std::memset(entry.path, 0, sizeof(entry.path));
    std::memcpy(entry.path, path.data(), path.size());
}

void add_directory(std::array<Entry, kEntryCount>& entries, std::uint32_t index, const std::string& path) {
    Entry& entry = entries.at(index);
    entry.used = 1;
    entry.type = 2;
    set_path(entry, path);
}

void add_file(std::vector<std::uint8_t>& disk, std::array<Entry, kEntryCount>& entries,
              std::uint32_t index, const std::string& path, const std::vector<std::uint8_t>& data) {
    if (data.size() > kMaxFileBytes) throw std::runtime_error("file too large");
    Entry& entry = entries.at(index);
    entry.used = 1;
    entry.type = 1;
    set_path(entry, path);
    entry.size = static_cast<std::uint32_t>(data.size());
    entry.checksum = fnv1a(data.data(), data.size());
    const std::uint32_t first_sector = kDataStart + index * kSlotSectors;
    const std::size_t offset = static_cast<std::size_t>(first_sector) * kSectorSize;
    if (offset + data.size() > disk.size()) throw std::runtime_error("ZenovFS layout exceeds disk size");
    std::copy(data.begin(), data.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<std::uint8_t> text_bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: zenovfs-builder <hello.zex> <output.img>\n";
            return 2;
        }
        const auto hello = read_all(argv[1]);
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(kTotalSectors) * kSectorSize, 0);
        std::array<Entry, kEntryCount> entries{};
        Superblock super{{'Z', 'E', 'N', 'O', 'V', 'F', 'S', '1'}, 1u, kTotalSectors,
                         kEntryCount, kEntrySectors, kDataStart, kSlotSectors, 1u,
                         {'Z', 'E', 'N', 'O', 'V', 'D', 'A', 'T', 'A', 0}, {0}};

        add_directory(entries, 0, "/apps");
        add_directory(entries, 1, "/docs");
        add_file(disk, entries, 2, "/docs/README.TXT", text_bytes(
            "ZenovFS persistent volume\n"
            "Files written under /data survive a reboot.\n"
            "Use ls, cat, write, append, mkdir, touch, rm, cp, mv and stat.\n"));
        add_file(disk, entries, 3, "/apps/HELLO.ZEX", hello);

        std::memcpy(disk.data(), &super, sizeof(super));
        std::memcpy(disk.data() + kSectorSize, entries.data(), sizeof(entries));

        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output image");
        output.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
        if (!output) throw std::runtime_error("cannot write output image");

        std::cout << "zenovfs-builder: OK sectors=" << kTotalSectors
                  << " entries=" << kEntryCount << " app=" << hello.size() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-builder: " << error.what() << "\n";
        return 1;
    }
}
