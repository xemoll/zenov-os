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
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }
    return hash;
}

std::vector<std::uint8_t> read_image(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size != static_cast<std::streamoff>(kExpectedSectors * kSectorSize))
        throw std::runtime_error("unexpected ZenovFS image size");
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

void verify_geometry(const Superblock& super, std::size_t disk_size) {
    const std::uint64_t disk_sectors = disk_size / kSectorSize;
    const std::uint64_t entry_bytes = static_cast<std::uint64_t>(super.entry_count) * sizeof(Entry);
    const std::uint64_t entry_capacity = static_cast<std::uint64_t>(super.entry_sectors) * kSectorSize;
    const std::uint64_t minimum_data_start = 1ULL + super.entry_sectors;
    const std::uint64_t final_sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(super.entry_count) * super.slot_sectors;
    if (super.total_sectors != disk_sectors || super.entry_count == 0U || super.entry_count > kMaxEntries ||
        super.entry_sectors == 0U || super.slot_sectors == 0U || entry_bytes > entry_capacity ||
        super.data_start < minimum_data_start || final_sector > disk_sectors)
        throw std::runtime_error("invalid ZenovFS geometry");
}

std::string file_data(const std::vector<std::uint8_t>& disk, const Superblock& super,
                      std::uint32_t index, const Entry& entry) {
    const std::uint64_t slot_bytes = static_cast<std::uint64_t>(super.slot_sectors) * kSectorSize;
    if (entry.size > slot_bytes) throw std::runtime_error("file exceeds slot: " + path_of(entry));
    const std::uint64_t sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(index) * super.slot_sectors;
    const std::uint64_t offset = sector * kSectorSize;
    if (offset > disk.size() || entry.size > disk.size() - offset) throw std::runtime_error("file outside image");
    if (fnv1a(disk.data() + offset, entry.size) != entry.checksum)
        throw std::runtime_error("checksum mismatch: " + path_of(entry));
    return std::string(reinterpret_cast<const char*>(disk.data() + offset), entry.size);
}

bool digits(const std::string& text, std::size_t offset, std::size_t count) {
    if (offset > text.size() || count > text.size() - offset) return false;
    for (std::size_t index = 0; index < count; ++index)
        if (text[offset + index] < '0' || text[offset + index] > '9') return false;
    return true;
}

unsigned parse_unsigned(const std::string& text, std::size_t offset, std::size_t count) {
    unsigned value = 0;
    for (std::size_t index = 0; index < count; ++index)
        value = value * 10U + static_cast<unsigned>(text[offset + index] - '0');
    return value;
}

bool leap(unsigned year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

unsigned month_days(unsigned year, unsigned month) {
    static constexpr unsigned days[12] = {31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
    return month == 2U && leap(year) ? 29U : days[month - 1U];
}

void verify_reminder_line(const std::string& line) {
    if (line.size() < 23U || line.size() > 62U || line.compare(0, 3, "R1|") != 0 ||
        line[3] != '1' || line[4] != '|' || line[9] != '-' || line[12] != '-' ||
        line[15] != '|' || line[18] != ':' || line[21] != '|')
        throw std::runtime_error("noncanonical completed reminder");
    if (!digits(line, 5, 4) || !digits(line, 10, 2) || !digits(line, 13, 2) ||
        !digits(line, 16, 2) || !digits(line, 19, 2))
        throw std::runtime_error("non-numeric reminder timestamp");
    const unsigned year = parse_unsigned(line, 5, 4);
    const unsigned month = parse_unsigned(line, 10, 2);
    const unsigned day = parse_unsigned(line, 13, 2);
    const unsigned hour = parse_unsigned(line, 16, 2);
    const unsigned minute = parse_unsigned(line, 19, 2);
    if (year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U ||
        day > month_days(year, month) || hour > 23U || minute > 59U)
        throw std::runtime_error("invalid reminder timestamp");
    if (line.substr(22) != "drink water") throw std::runtime_error("reminder title mismatch");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: zenovfs-productivity-utilities-verify <runtime-data.img>\n";
            return 2;
        }
        const auto disk = read_image(argv[1]);
        const auto* super = reinterpret_cast<const Superblock*>(disk.data());
        const char expected[8] = {'Z','E','N','O','V','F','S','1'};
        if (std::memcmp(super->magic, expected, sizeof(expected)) != 0 || super->version != 1U)
            throw std::runtime_error("invalid ZenovFS superblock");
        verify_geometry(*super, disk.size());

        const auto* entries = reinterpret_cast<const Entry*>(disk.data() + kSectorSize);
        std::map<std::string, std::string> files;
        bool utilities_directory = false;
        bool reminders_directory = false;
        for (std::uint32_t index = 0; index < super->entry_count; ++index) {
            const Entry& entry = entries[index];
            if (!entry.used) continue;
            if (entry.type != kFile && entry.type != kDirectory) throw std::runtime_error("unknown entry type");
            const std::string path = path_of(entry);
            if (entry.type == kDirectory) {
                if (entry.size != 0U) throw std::runtime_error("directory carries payload");
                if (path == "/utilities") utilities_directory = true;
                if (path == "/reminders") reminders_directory = true;
            } else {
                const auto [unused, inserted] = files.emplace(path, file_data(disk, *super, index, entry));
                (void)unused;
                if (!inserted) throw std::runtime_error("duplicate file path: " + path);
            }
        }
        if (!utilities_directory || !reminders_directory) throw std::runtime_error("utility directories missing");

        const auto calculator = files.find("/utilities/calculator.state");
        if (calculator == files.end() || calculator->second.find("C1\t0\n") == std::string::npos ||
            calculator->second.find("H\t2+3*4\t14\n") == std::string::npos ||
            calculator->second.find("H\t0xf<<2\t60\n") == std::string::npos)
            throw std::runtime_error("calculator memory/history persistence missing");

        const auto reminders = files.find("/reminders/reminders.db");
        if (reminders == files.end()) throw std::runtime_error("reminder database missing");
        std::size_t start = 0;
        unsigned lines = 0;
        while (start < reminders->second.size()) {
            const std::size_t end = reminders->second.find('\n', start);
            if (end == std::string::npos) throw std::runtime_error("reminder database lacks final newline");
            if (end > start) {
                verify_reminder_line(reminders->second.substr(start, end - start));
                ++lines;
            }
            start = end + 1U;
        }
        if (lines != 1U) throw std::runtime_error("unexpected reminder count");

        std::cout << "ZENOV_PRODUCTIVITY_UTILITIES_RUNTIME_IMAGE_OK calculator=history+memory reminders=canonical+done+snoozed reboot=persistent checksum=valid geometry=bounded\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-productivity-utilities-verify: " << error.what() << "\n";
        return 1;
    }
}
