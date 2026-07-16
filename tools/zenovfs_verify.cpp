#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kExpectedSectors = 32768;
constexpr std::uint32_t kMaxEntries = 128;

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

std::vector<std::uint8_t> read_image(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size != static_cast<std::streamoff>(kExpectedSectors * kSectorSize)) {
        throw std::runtime_error("unexpected ZenovFS image size");
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("failed reading image");
    return bytes;
}

std::string path_of(const Entry& entry) {
    const auto end = std::find(std::begin(entry.path), std::end(entry.path), '\0');
    if (end == std::end(entry.path)) throw std::runtime_error("unterminated entry path");
    return std::string(entry.path, end);
}

bool valid_path(const std::string& path) {
    if (path.size() < 2 || path.front() != '/' || path.back() == '/' || path.find("//") != std::string::npos) return false;
    for (unsigned char value : path) {
        if (value < 32 || value > 126) return false;
    }
    return true;
}

std::string parent_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: zenovfs-verify <zenov-data.img>\n";
            return 2;
        }
        const auto disk = read_image(argv[1]);
        const auto* super = reinterpret_cast<const Superblock*>(disk.data());
        const char expected[8] = {'Z','E','N','O','V','F','S','1'};
        if (std::memcmp(super->magic, expected, sizeof(expected)) != 0 || super->version != 1 ||
            super->total_sectors != kExpectedSectors || super->entry_count == 0 ||
            super->entry_count > kMaxEntries || super->entry_sectors * kSectorSize < super->entry_count * sizeof(Entry) ||
            super->slot_sectors == 0 || super->data_start < 1 + super->entry_sectors) {
            throw std::runtime_error("invalid ZenovFS superblock");
        }

        const auto* entries = reinterpret_cast<const Entry*>(disk.data() + kSectorSize);
        std::set<std::string> paths;
        std::set<std::string> directories{"/"};
        std::uint32_t files = 0;
        std::uint32_t directory_count = 0;

        for (std::uint32_t i = 0; i < super->entry_count; ++i) {
            const Entry& entry = entries[i];
            if (!entry.used) continue;
            const std::string path = path_of(entry);
            if (!valid_path(path) || !paths.insert(path).second) throw std::runtime_error("invalid or duplicate entry path: " + path);
            if (entry.type == 2) {
                if (entry.size != 0 || entry.checksum != 0) throw std::runtime_error("directory metadata is not empty: " + path);
                directories.insert(path);
                ++directory_count;
            } else if (entry.type == 1) {
                const std::uint64_t capacity = static_cast<std::uint64_t>(super->slot_sectors) * kSectorSize;
                if (entry.size > capacity) throw std::runtime_error("file exceeds slot: " + path);
                const std::uint64_t sector = static_cast<std::uint64_t>(super->data_start) + static_cast<std::uint64_t>(i) * super->slot_sectors;
                const std::uint64_t offset = sector * kSectorSize;
                if (offset + entry.size > disk.size()) throw std::runtime_error("file data outside image: " + path);
                if (fnv1a(disk.data() + offset, entry.size) != entry.checksum) throw std::runtime_error("checksum mismatch: " + path);
                ++files;
            } else {
                throw std::runtime_error("unsupported entry type: " + path);
            }
        }

        for (const auto& path : paths) {
            const std::string parent = parent_of(path);
            if (directories.find(parent) == directories.end()) throw std::runtime_error("missing parent directory for " + path);
        }
        for (const std::string required : {"/apps/hello.zex", "/apps/fileio.elf", "/config/system.ini", "/docs/release.txt"}) {
            if (paths.find(required) == paths.end()) throw std::runtime_error("required seed missing: " + required);
        }

        std::cout << "zenovfs-verify: OK version=1 generation=" << super->generation
                  << " files=" << files << " directories=" << directory_count << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-verify: " << error.what() << "\n";
        return 1;
    }
}
