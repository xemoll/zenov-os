#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kExpectedSectors = 32768;
constexpr std::uint32_t kMaxEntries = 128;
constexpr std::uint8_t kFile = 1;
constexpr std::uint8_t kDirectory = 2;

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
    if (size < 0 || size != static_cast<std::streamoff>(kExpectedSectors * kSectorSize)) {
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
    if (end == std::begin(entry.path) || entry.path[0] != '/') throw std::runtime_error("noncanonical entry path");
    return std::string(entry.path, end);
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool date_note_path(const std::string& path) {
    if (path.size() != 20 || !starts_with(path, "/notes/") || path.substr(17) != ".md") return false;
    for (std::size_t i = 7; i < 17; ++i) {
        if (i == 11 || i == 14) {
            if (path[i] != '-') return false;
        } else if (path[i] < '0' || path[i] > '9') return false;
    }
    return true;
}

void verify_geometry(const Superblock& super, std::size_t disk_size) {
    const std::uint64_t disk_sectors = disk_size / kSectorSize;
    const std::uint64_t entry_bytes = static_cast<std::uint64_t>(super.entry_count) * sizeof(Entry);
    const std::uint64_t entry_capacity = static_cast<std::uint64_t>(super.entry_sectors) * kSectorSize;
    const std::uint64_t minimum_data_start = 1ULL + super.entry_sectors;
    const std::uint64_t final_sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(super.entry_count) * super.slot_sectors;
    if (super.total_sectors != disk_sectors || super.entry_count == 0U || super.entry_count > kMaxEntries ||
        super.entry_sectors == 0U || super.slot_sectors == 0U || entry_bytes > entry_capacity ||
        super.data_start < minimum_data_start || final_sector > disk_sectors) {
        throw std::runtime_error("invalid ZenovFS geometry");
    }
}

std::string file_data(const std::vector<std::uint8_t>& disk, const Superblock& super,
                      std::uint32_t index, const Entry& entry) {
    const std::uint64_t slot_bytes = static_cast<std::uint64_t>(super.slot_sectors) * kSectorSize;
    if (entry.size > slot_bytes) throw std::runtime_error("file exceeds slot: " + path_of(entry));
    const std::uint64_t sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(index) * super.slot_sectors;
    const std::uint64_t offset = sector * kSectorSize;
    if (offset > disk.size() || entry.size > disk.size() - offset) throw std::runtime_error("file outside image");
    if (fnv1a(disk.data() + offset, entry.size) != entry.checksum) {
        throw std::runtime_error("checksum mismatch: " + path_of(entry));
    }
    return std::string(reinterpret_cast<const char*>(disk.data() + offset), entry.size);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: zenovfs-productivity-verify <runtime-data.img>\n";
            return 2;
        }
        const auto disk = read_image(argv[1]);
        const auto* super = reinterpret_cast<const Superblock*>(disk.data());
        const char expected[8] = {'Z','E','N','O','V','F','S','1'};
        if (std::memcmp(super->magic, expected, sizeof(expected)) != 0 || super->version != 1U) {
            throw std::runtime_error("invalid ZenovFS superblock");
        }
        verify_geometry(*super, disk.size());

        const auto* entries = reinterpret_cast<const Entry*>(disk.data() + kSectorSize);
        std::map<std::string, std::string> files;
        bool notes_directory = false;
        bool calendar_directory = false;
        for (std::uint32_t i = 0; i < super->entry_count; ++i) {
            const Entry& entry = entries[i];
            if (!entry.used) continue;
            if (entry.type != kFile && entry.type != kDirectory) throw std::runtime_error("unknown entry type");
            const std::string path = path_of(entry);
            if (entry.type == kDirectory) {
                if (entry.size != 0U) throw std::runtime_error("directory carries payload");
                if (path == "/notes") notes_directory = true;
                if (path == "/calendar") calendar_directory = true;
            } else {
                const auto [unused, inserted] = files.emplace(path, file_data(disk, *super, i, entry));
                (void)unused;
                if (!inserted) throw std::runtime_error("duplicate file path: " + path);
            }
        }

        if (!notes_directory || !calendar_directory) throw std::runtime_error("productivity directories missing");
        const auto scratch = files.find("/notes/scratch.txt");
        if (scratch == files.end() || scratch->second.find("scratch") == std::string::npos) {
            throw std::runtime_error("scratchpad persistence missing");
        }
        const auto events = files.find("/calendar/events.db");
        if (events == files.end() || events->second.find("|meet\n") == std::string::npos) {
            throw std::runtime_error("calendar event persistence missing");
        }
        const auto tasks = files.find("/notes/tasks.md");
        const std::string expected_task = "- [x] ship #P1 #D-2099-12-31 updated\n";
        if (tasks == files.end() || tasks->second.find(expected_task) == std::string::npos ||
            tasks->second.find("- [ ] ship #P1 #D-2099-12-31 updated\n") != std::string::npos ||
            tasks->second.find("temporary") != std::string::npos) {
            throw std::runtime_error("Markdown task CRUD persistence missing");
        }

        bool normal_note = false;
        bool daily_note = false;
        for (const auto& [path, content] : files) {
            if (starts_with(path, "/notes/note-") && path.size() > 10 && path.substr(path.size() - 3) == ".md" &&
                content.find("test") != std::string::npos && content.find("tags: []") != std::string::npos) {
                normal_note = true;
            }
            if (date_note_path(path) && content.find("tags: []") != std::string::npos) daily_note = true;
        }
        if (!normal_note) throw std::runtime_error("normal Markdown note persistence missing");
        if (!daily_note) throw std::runtime_error("daily note persistence missing");

        std::cout << "ZENOV_PRODUCTIVITY_RUNTIME_IMAGE_OK notes=markdown+scratch+daily tasks=smart-views+guarded-crud calendar=events checksum=valid geometry=bounded\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-productivity-verify: " << error.what() << "\n";
        return 1;
    }
}
