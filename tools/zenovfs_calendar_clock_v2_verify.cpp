#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSector = 512U;
constexpr std::uint32_t kSectors = 32768U;
#pragma pack(push, 1)
struct Superblock {
    char magic[8]; std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation;
    char label[16]; std::uint8_t reserved[460];
};
struct Entry {
    std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved;
};
#pragma pack(pop)
static_assert(sizeof(Superblock) == 512U);
static_assert(sizeof(Entry) == 64U);

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < size; ++index) { hash ^= data[index]; hash *= 16777619U; }
    return hash;
}

std::vector<std::uint8_t> read_image(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size != static_cast<std::streamoff>(kSector * kSectors)) throw std::runtime_error("unexpected image size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input) throw std::runtime_error("image read failed");
    return bytes;
}

std::string entry_path(const Entry& entry) {
    const auto end = std::find(std::begin(entry.path), std::end(entry.path), '\0');
    if (end == std::end(entry.path)) throw std::runtime_error("unterminated path");
    return std::string(entry.path, end);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) { std::cerr << "usage: zenovfs-calendar-clock-v2-verify <image>\n"; return 2; }
        const auto disk = read_image(argv[1]);
        const auto* super = reinterpret_cast<const Superblock*>(disk.data());
        const char magic[8] = {'Z','E','N','O','V','F','S','1'};
        if (std::memcmp(super->magic, magic, 8U) != 0 || super->version != 1U || super->total_sectors != kSectors ||
            !super->entry_count || super->entry_count > 128U || !super->slot_sectors) throw std::runtime_error("invalid superblock");
        const auto* entries = reinterpret_cast<const Entry*>(disk.data() + kSector);
        std::map<std::string, std::string> files;
        for (std::uint32_t index = 0U; index < super->entry_count; ++index) {
            const Entry& entry = entries[index];
            if (!entry.used || entry.type != 1U) continue;
            const std::uint64_t offset = static_cast<std::uint64_t>(super->data_start + index * super->slot_sectors) * kSector;
            if (offset + entry.size > disk.size()) throw std::runtime_error("entry outside image");
            if (fnv1a(disk.data() + offset, entry.size) != entry.checksum) throw std::runtime_error("checksum mismatch");
            files.emplace(entry_path(entry), std::string(reinterpret_cast<const char*>(disk.data() + offset), entry.size));
        }
        const auto event_v2 = files.find("/calendar/events-v2.db");
        const auto projection = files.find("/calendar/events.db");
        const auto alarms = files.find("/clock/alarms.db");
        if (event_v2 == files.end() || event_v2->second != "E2|2026-08-04|09:30|0090|W|04|meet\n")
            throw std::runtime_error("timed recurring calendar source mismatch");
        if (projection == files.end() || projection->second != "2026-08-04|meet\n")
            throw std::runtime_error("Agenda calendar projection mismatch");
        if (alarms == files.end() || alarms->second != "A1|12:00|0|000|test alarm\n")
            throw std::runtime_error("persistent one-shot alarm mismatch");
        std::cout << "ZENOV_CALENDAR_CLOCK_V2_RUNTIME_IMAGE_OK calendar=E2+timed+duration+weekly projection=agenda-compatible clock=alarm-once+disabled-after-fire checksum=valid generation=" << super->generation << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-calendar-clock-v2-verify: " << error.what() << "\n";
        return 1;
    }
}
