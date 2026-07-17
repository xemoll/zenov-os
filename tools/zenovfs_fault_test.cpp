#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kEntryCount = 128;
constexpr std::uint8_t kFile = 1;
constexpr std::uint8_t kTransaction = 3;
constexpr std::uint16_t kCommitted = 1;
#pragma pack(push, 1)
struct Superblock {
    char magic[8];
    std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation;
    char label[16];
    std::uint8_t reserved[460];
};
struct Entry {
    std::uint8_t used, type;
    std::uint16_t flags;
    char path[48];
    std::uint32_t size, checksum, reserved;
};
#pragma pack(pop)
static_assert(sizeof(Superblock) == 512);
static_assert(sizeof(Entry) == 64);
struct Write { std::uint32_t sector; std::array<std::uint8_t, kSectorSize> bytes; };

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("cannot size image");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input) throw std::runtime_error("cannot read image");
    return data;
}
std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}
Entry* entries(std::vector<std::uint8_t>& disk) { return reinterpret_cast<Entry*>(disk.data() + kSectorSize); }
const Entry* entries(const std::vector<std::uint8_t>& disk) { return reinterpret_cast<const Entry*>(disk.data() + kSectorSize); }
const Superblock& super(const std::vector<std::uint8_t>& disk) { return *reinterpret_cast<const Superblock*>(disk.data()); }
std::uint32_t metadata_sector(std::uint32_t index) { return 1u + index / 8u; }
std::uint32_t slot_offset(std::uint32_t index) { return (index % 8u) * sizeof(Entry); }
std::uint32_t data_sector(const Superblock& sb, std::uint32_t index) { return sb.data_start + index * sb.slot_sectors; }
void apply_write(std::vector<std::uint8_t>& disk, const Write& write) {
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (offset + kSectorSize > disk.size()) throw std::runtime_error("write outside image");
    std::copy(write.bytes.begin(), write.bytes.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
}
Write sector_snapshot(const std::vector<std::uint8_t>& disk, std::uint32_t sector) {
    Write write{sector, {}};
    const std::size_t offset = static_cast<std::size_t>(sector) * kSectorSize;
    std::copy_n(disk.begin() + static_cast<std::ptrdiff_t>(offset), kSectorSize, write.bytes.begin());
    return write;
}
void set_entry_write(std::vector<std::uint8_t>& planning, std::vector<Write>& writes, std::uint32_t index, const Entry& entry) {
    const std::uint32_t sector = metadata_sector(index);
    const std::size_t offset = static_cast<std::size_t>(sector) * kSectorSize + slot_offset(index);
    std::memcpy(planning.data() + offset, &entry, sizeof(entry));
    writes.push_back(sector_snapshot(planning, sector));
}
void clear_entry_write(std::vector<std::uint8_t>& planning, std::vector<Write>& writes, std::uint32_t index) {
    Entry empty{};
    set_entry_write(planning, writes, index, empty);
}
bool recover(std::vector<std::uint8_t>& disk) {
    const auto& sb = super(disk);
    auto* table = entries(disk);
    bool changed = false;
    for (std::uint32_t i = 0; i < sb.entry_count; ++i) {
        if (table[i].used && table[i].type == kTransaction) { table[i] = Entry{}; changed = true; }
    }
    for (std::uint32_t i = 0; i < sb.entry_count; ++i) {
        Entry& item = table[i];
        if (!item.used || item.type != kFile || !(item.flags & kCommitted)) continue;
        if (item.reserved < sb.entry_count && item.reserved != i) table[item.reserved] = Entry{};
        item.flags = 0; item.reserved = 0; changed = true;
    }
    return changed;
}
std::vector<std::uint8_t> read_file(const std::vector<std::uint8_t>& disk, const std::string& path) {
    const auto& sb = super(disk);
    const auto* table = entries(disk);
    int found = -1;
    for (std::uint32_t i = 0; i < sb.entry_count; ++i) {
        if (!table[i].used || table[i].type != kFile) continue;
        if (path == table[i].path) {
            if (found >= 0) throw std::runtime_error("duplicate committed path");
            found = static_cast<int>(i);
        }
    }
    if (found < 0) throw std::runtime_error("target path missing");
    const Entry& item = table[found];
    if (item.flags || item.size > sb.slot_sectors * kSectorSize) throw std::runtime_error("invalid committed metadata");
    const std::size_t offset = static_cast<std::size_t>(data_sector(sb, static_cast<std::uint32_t>(found))) * kSectorSize;
    if (offset + item.size > disk.size()) throw std::runtime_error("payload outside image");
    std::vector<std::uint8_t> data(item.size);
    std::copy_n(disk.begin() + static_cast<std::ptrdiff_t>(offset), item.size, data.begin());
    if (fnv1a(data.data(), data.size()) != item.checksum) throw std::runtime_error("checksum mismatch");
    return data;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) { std::cerr << "usage: zenovfs-fault-test <zenov-data.img>\n"; return 2; }
        const auto original = read_all(argv[1]);
        if (original.size() < kSectorSize || std::memcmp(original.data(), "ZENOVFS1", 8) != 0) throw std::runtime_error("not a ZenovFS1 image");
        const auto& sb = super(original);
        if (sb.entry_count == 0 || sb.entry_count > kEntryCount) throw std::runtime_error("invalid entry count");
        const std::string path = "/config/system.ini";
        const auto old_content = read_file(original, path);
        const std::string replacement = "[system]\nversion=0.1.1\ntransaction=cow\n" + std::string(900, 'x') + "\n";
        const std::vector<std::uint8_t> new_content(replacement.begin(), replacement.end());
        int old_index = -1, staging_index = -1;
        const auto* original_entries = entries(original);
        for (std::uint32_t i = 0; i < sb.entry_count; ++i) {
            if (original_entries[i].used && original_entries[i].type == kFile && path == original_entries[i].path) old_index = static_cast<int>(i);
            if (staging_index < 0 && !original_entries[i].used) staging_index = static_cast<int>(i);
        }
        if (old_index < 0 || staging_index < 0) throw std::runtime_error("missing old or staging slot");
        std::vector<std::uint8_t> planning = original;
        std::vector<Write> writes;
        std::size_t copied = 0;
        for (std::uint32_t sector = 0; copied < new_content.size(); ++sector) {
            Write write{data_sector(sb, static_cast<std::uint32_t>(staging_index)) + sector, {}};
            const std::size_t chunk = std::min<std::size_t>(kSectorSize, new_content.size() - copied);
            std::copy_n(new_content.begin() + static_cast<std::ptrdiff_t>(copied), chunk, write.bytes.begin());
            apply_write(planning, write); writes.push_back(write); copied += chunk;
        }
        Entry stage{};
        stage.used = 1; stage.type = kTransaction; stage.size = static_cast<std::uint32_t>(new_content.size()); stage.checksum = fnv1a(new_content.data(), new_content.size()); stage.reserved = static_cast<std::uint32_t>(old_index);
        std::memcpy(stage.path, path.data(), path.size());
        set_entry_write(planning, writes, static_cast<std::uint32_t>(staging_index), stage);
        stage.type = kFile; stage.flags = kCommitted;
        set_entry_write(planning, writes, static_cast<std::uint32_t>(staging_index), stage);
        clear_entry_write(planning, writes, static_cast<std::uint32_t>(old_index));
        stage.flags = 0; stage.reserved = 0;
        set_entry_write(planning, writes, static_cast<std::uint32_t>(staging_index), stage);

        bool observed_recovery = false;
        for (std::size_t prefix = 0; prefix <= writes.size(); ++prefix) {
            auto crashed = original;
            for (std::size_t i = 0; i < prefix; ++i) apply_write(crashed, writes[i]);
            observed_recovery = recover(crashed) || observed_recovery;
            const auto content = read_file(crashed, path);
            if (content != old_content && content != new_content) throw std::runtime_error("crash prefix exposed partial content: " + std::to_string(prefix));
        }
        std::cout << "ZENOVFS_FAULT_INJECTION_OK boundaries=" << (writes.size() + 1) << "\n";
        std::cout << "ZENOVFS_OLD_OR_NEW_CONTENT_ONLY\n";
        if (!observed_recovery) throw std::runtime_error("no interrupted transaction required recovery");
        std::cout << "ZENOVFS_INTERRUPTED_WRITE_RECOVERED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-fault-test: " << error.what() << "\n";
        return 1;
    }
}
