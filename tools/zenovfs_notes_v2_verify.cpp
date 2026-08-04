#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t sector_size = 512U;
constexpr std::uint32_t expected_sectors = 32768U;
constexpr std::uint32_t max_entries = 128U;
constexpr std::uint8_t file_type = 1U;
constexpr std::uint8_t directory_type = 2U;

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
static_assert(sizeof(Superblock) == sector_size);
static_assert(sizeof(Entry) == 64U);

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

std::vector<std::uint8_t> read_image(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff expected = static_cast<std::streamoff>(expected_sectors) * sector_size;
    if (size != expected) throw std::runtime_error("unexpected image size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("failed reading image");
    return bytes;
}

std::string entry_path(const Entry& entry) {
    const char* end = std::find(std::begin(entry.path), std::end(entry.path), '\0');
    if (end == std::end(entry.path) || end == std::begin(entry.path) || entry.path[0] != '/')
        throw std::runtime_error("invalid entry path");
    return std::string(entry.path, end);
}

void verify_geometry(const Superblock& super, std::size_t bytes) {
    const std::uint64_t disk_sectors = bytes / sector_size;
    const std::uint64_t entry_bytes = static_cast<std::uint64_t>(super.entry_count) * sizeof(Entry);
    const std::uint64_t entry_capacity = static_cast<std::uint64_t>(super.entry_sectors) * sector_size;
    const std::uint64_t final_sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(super.entry_count) * super.slot_sectors;
    if (super.total_sectors != disk_sectors || !super.entry_count || super.entry_count > max_entries ||
        !super.entry_sectors || !super.slot_sectors || entry_bytes > entry_capacity ||
        super.data_start < 1U + super.entry_sectors || final_sector > disk_sectors)
        throw std::runtime_error("invalid ZenovFS geometry");
}

std::string read_file(const std::vector<std::uint8_t>& disk, const Superblock& super,
                      std::uint32_t index, const Entry& entry) {
    const std::uint64_t slot_bytes = static_cast<std::uint64_t>(super.slot_sectors) * sector_size;
    if (entry.size > slot_bytes) throw std::runtime_error("file exceeds slot");
    const std::uint64_t sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(index) * super.slot_sectors;
    const std::uint64_t offset = sector * sector_size;
    if (offset > disk.size() || entry.size > disk.size() - offset) throw std::runtime_error("file outside image");
    if (fnv1a(disk.data() + offset, entry.size) != entry.checksum) throw std::runtime_error("checksum mismatch");
    return std::string(reinterpret_cast<const char*>(disk.data() + offset), entry.size);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: zenovfs-notes-v2-verify <runtime-data.img>\n";
            return 2;
        }
        const auto disk = read_image(argv[1]);
        const auto* super = reinterpret_cast<const Superblock*>(disk.data());
        const char magic[8] = {'Z','E','N','O','V','F','S','1'};
        if (std::memcmp(super->magic, magic, sizeof(magic)) != 0 || super->version != 1U)
            throw std::runtime_error("invalid ZenovFS superblock");
        verify_geometry(*super, disk.size());

        const auto* entries = reinterpret_cast<const Entry*>(disk.data() + sector_size);
        bool notes_directory = false;
        bool scratch_found = false;
        std::string scratch;
        for (std::uint32_t index = 0U; index < super->entry_count; ++index) {
            const Entry& entry = entries[index];
            if (!entry.used) continue;
            if (entry.type != file_type && entry.type != directory_type) throw std::runtime_error("unknown entry type");
            const std::string path = entry_path(entry);
            if (entry.type == directory_type) {
                if (entry.size) throw std::runtime_error("directory carries data");
                if (path == "/notes") notes_directory = true;
                continue;
            }
            if (path == "/notes/scratch.txt") {
                if (scratch_found) throw std::runtime_error("duplicate scratchpad");
                scratch = read_file(disk, *super, index, entry);
                scratch_found = true;
            }
        }

        if (!notes_directory || !scratch_found) throw std::runtime_error("Notes storage missing");
        const std::string expected = "alpha\nXgammaY\ndeltaQz";
        if (scratch.find(expected) == std::string::npos) throw std::runtime_error("cursor edit sequence missing");
        if (scratch.find("\ngama\n") != std::string::npos || scratch.find("deltaX") != std::string::npos ||
            scratch.find("\ndetaQ") != std::string::npos)
            throw std::runtime_error("intermediate edit state leaked into persistence");

        std::cout << "ZENOV_NOTES_V2_RUNTIME_IMAGE_OK scratch=cursor-edit+undo+redo persistence=reboot checksum=valid geometry=bounded generation="
                  << super->generation << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-notes-v2-verify: " << error.what() << '\n';
        return 1;
    }
}
