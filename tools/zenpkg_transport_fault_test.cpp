#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"

namespace {
constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kEntryLimit = 128U;
constexpr std::uint8_t kFile = 1U;
constexpr std::uint8_t kDirectory = 2U;
constexpr std::uint8_t kTransaction = 3U;
constexpr std::uint16_t kCommitted = 1U;
constexpr std::uint32_t kNoEntry = 0xFFFFFFFFU;
constexpr char kSourcePath[] = "/packages/hello-native-0.2.0.zpk";
constexpr char kJournalPath[] = "/var/lib/zenpkg/transport.v1";
constexpr char kJournalSourcePath[] = "/data/packages/hello-native-0.2.0.zpk";
constexpr char kPackageName[] = "hello-native";
constexpr char kPackageVersion[] = "0.2.0";

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
struct TransportJournal {
    char magic[8];
    std::uint32_t schema, generation, phase, offset, expected_length, provider, retries, checksum;
    char name[32], version[16], source_path[48], partial_path[48];
    std::uint8_t package_digest[32], prefix_digest[32];
};
#pragma pack(pop)

static_assert(sizeof(Superblock) == kSectorSize);
static_assert(sizeof(Entry) == 64U);
static_assert(sizeof(TransportJournal) == 248U);

struct Write {
    std::uint32_t sector = 0;
    std::array<std::uint8_t, kSectorSize> bytes{};
};

struct Plan {
    std::vector<Write> writes;
    std::size_t resumable_prefix = 0;
    std::size_t commit_recovery_prefix = 0;
};

std::vector<std::uint8_t> read_image(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < static_cast<std::streamoff>(kSectorSize)) throw std::runtime_error("image too small");
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read image");
    return bytes;
}

void write_image(const std::string& path, const std::vector<std::uint8_t>& image) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create output image");
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!output) throw std::runtime_error("cannot write output image");
}

Superblock& super(std::vector<std::uint8_t>& disk) {
    return *reinterpret_cast<Superblock*>(disk.data());
}
const Superblock& super(const std::vector<std::uint8_t>& disk) {
    return *reinterpret_cast<const Superblock*>(disk.data());
}
Entry* entries(std::vector<std::uint8_t>& disk) {
    return reinterpret_cast<Entry*>(disk.data() + kSectorSize);
}
const Entry* entries(const std::vector<std::uint8_t>& disk) {
    return reinterpret_cast<const Entry*>(disk.data() + kSectorSize);
}

std::uint32_t metadata_sector(std::uint32_t index) { return 1U + index / 8U; }
std::uint32_t slot_offset(std::uint32_t index) { return (index % 8U) * sizeof(Entry); }
std::uint32_t data_sector(const Superblock& block, std::uint32_t index) {
    return block.data_start + index * block.slot_sectors;
}

void validate_geometry(const std::vector<std::uint8_t>& disk) {
    if (disk.size() < kSectorSize || std::memcmp(disk.data(), "ZENOVFS1", 8U) != 0)
        throw std::runtime_error("not a ZenovFS1 image");
    const Superblock& block = super(disk);
    if (block.version != 1U || !block.entry_count || block.entry_count > kEntryLimit ||
        !block.entry_sectors || block.entry_sectors * kSectorSize < block.entry_count * sizeof(Entry) ||
        !block.slot_sectors || block.data_start < 1U + block.entry_sectors ||
        block.total_sectors * static_cast<std::uint64_t>(kSectorSize) > disk.size()) {
        throw std::runtime_error("invalid ZenovFS geometry");
    }
    const std::uint64_t final_sector = static_cast<std::uint64_t>(block.data_start) +
        static_cast<std::uint64_t>(block.entry_count) * block.slot_sectors;
    if (final_sector > block.total_sectors) throw std::runtime_error("ZenovFS slots exceed image");
}

std::string entry_path(const Entry& entry) {
    const auto end = std::find(std::begin(entry.path), std::end(entry.path), '\0');
    if (end == std::end(entry.path)) throw std::runtime_error("unterminated path");
    return std::string(entry.path, end);
}

void set_path(Entry& entry, const std::string& path) {
    if (path.size() < 2U || path.size() >= sizeof(entry.path) || path.front() != '/' ||
        path.back() == '/' || path.find("//") != std::string::npos) {
        throw std::runtime_error("unsafe path: " + path);
    }
    std::memset(entry.path, 0, sizeof(entry.path));
    std::memcpy(entry.path, path.data(), path.size());
}

int find_entry(const std::vector<std::uint8_t>& disk, const std::string& path) {
    const Superblock& block = super(disk);
    const Entry* table = entries(disk);
    int found = -1;
    for (std::uint32_t i = 0; i < block.entry_count; ++i) {
        if (!table[i].used || table[i].type == kTransaction) continue;
        if (entry_path(table[i]) == path) {
            if (found >= 0) throw std::runtime_error("duplicate path: " + path);
            found = static_cast<int>(i);
        }
    }
    return found;
}

int find_free(const std::vector<std::uint8_t>& disk, std::uint32_t excluded = kNoEntry) {
    const Superblock& block = super(disk);
    const Entry* table = entries(disk);
    for (std::uint32_t i = 0; i < block.entry_count; ++i) {
        if (i != excluded && !table[i].used) return static_cast<int>(i);
    }
    return -1;
}

std::vector<std::uint8_t> read_file(const std::vector<std::uint8_t>& disk, const std::string& path) {
    const int found = find_entry(disk, path);
    if (found < 0) throw std::runtime_error("file missing: " + path);
    const Entry& entry = entries(disk)[static_cast<std::uint32_t>(found)];
    if (entry.type != kFile || entry.flags || entry.size > super(disk).slot_sectors * kSectorSize)
        throw std::runtime_error("invalid file metadata: " + path);
    const std::size_t offset = static_cast<std::size_t>(
        data_sector(super(disk), static_cast<std::uint32_t>(found))) * kSectorSize;
    if (offset + entry.size > disk.size()) throw std::runtime_error("file outside image: " + path);
    std::vector<std::uint8_t> bytes(entry.size);
    std::copy_n(disk.begin() + static_cast<std::ptrdiff_t>(offset), entry.size, bytes.begin());
    if (zenov_audit_host::fnv1a(bytes.data(), bytes.size()) != entry.checksum)
        throw std::runtime_error("file checksum mismatch: " + path);
    return bytes;
}

void add_directory_baseline(std::vector<std::uint8_t>& disk, const std::string& path) {
    const int existing = find_entry(disk, path);
    if (existing >= 0) {
        if (entries(disk)[static_cast<std::uint32_t>(existing)].type != kDirectory)
            throw std::runtime_error("baseline path is not a directory: " + path);
        return;
    }
    const int free = find_free(disk);
    if (free < 0) throw std::runtime_error("no free entry for baseline directory");
    Entry& entry = entries(disk)[static_cast<std::uint32_t>(free)];
    std::memset(&entry, 0, sizeof(entry));
    entry.used = 1U;
    entry.type = kDirectory;
    set_path(entry, path);
}

void clear_baseline_path(std::vector<std::uint8_t>& disk, const std::string& path) {
    const int found = find_entry(disk, path);
    if (found >= 0) entries(disk)[static_cast<std::uint32_t>(found)] = Entry{};
}

void apply_full(std::vector<std::uint8_t>& disk, const Write& write) {
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (offset + kSectorSize > disk.size()) throw std::runtime_error("write outside image");
    std::copy(write.bytes.begin(), write.bytes.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
}

void apply_torn(std::vector<std::uint8_t>& disk, const Write& write, std::size_t count, bool suffix) {
    if (!count || count >= kSectorSize) throw std::runtime_error("invalid torn write");
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (offset + kSectorSize > disk.size()) throw std::runtime_error("torn write outside image");
    const std::size_t start = suffix ? kSectorSize - count : 0U;
    std::copy_n(write.bytes.begin() + static_cast<std::ptrdiff_t>(start), count,
        disk.begin() + static_cast<std::ptrdiff_t>(offset + start));
}

void apply_garbage(std::vector<std::uint8_t>& disk, const Write& write, std::uint32_t seed) {
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (offset + kSectorSize > disk.size()) throw std::runtime_error("garbage write outside image");
    for (std::size_t i = 0; i < kSectorSize; ++i)
        disk[offset + i] = static_cast<std::uint8_t>((seed * 37U + i * 109U + 0xA5U) & 0xFFU);
}

Write sector_snapshot(const std::vector<std::uint8_t>& disk, std::uint32_t sector) {
    Write write{};
    write.sector = sector;
    const std::size_t offset = static_cast<std::size_t>(sector) * kSectorSize;
    std::copy_n(disk.begin() + static_cast<std::ptrdiff_t>(offset), kSectorSize, write.bytes.begin());
    return write;
}

void record_sector(std::vector<std::uint8_t>& planning, std::vector<Write>& writes, std::uint32_t sector) {
    writes.push_back(sector_snapshot(planning, sector));
}

void set_entry_write(std::vector<std::uint8_t>& planning, std::vector<Write>& writes,
    std::uint32_t index, const Entry& entry) {
    const std::uint32_t sector = metadata_sector(index);
    const std::size_t offset = static_cast<std::size_t>(sector) * kSectorSize + slot_offset(index);
    std::memcpy(planning.data() + offset, &entry, sizeof(entry));
    record_sector(planning, writes, sector);
}

void sync_metadata_plan(std::vector<std::uint8_t>& planning, std::vector<Write>& writes) {
    const Superblock before = super(planning);
    for (std::uint32_t sector = 0; sector < before.entry_sectors; ++sector)
        record_sector(planning, writes, 1U + sector);
    Superblock committed = before;
    if (committed.generation == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("superblock generation exhausted");
    ++committed.generation;
    super(planning) = committed;
    record_sector(planning, writes, 0U);
}

void write_file_plan(std::vector<std::uint8_t>& planning, std::vector<Write>& writes,
    const std::string& path, const std::vector<std::uint8_t>& input, bool append) {
    const int old_index = find_entry(planning, path);
    std::vector<std::uint8_t> data;
    if (append && old_index >= 0) data = read_file(planning, path);
    data.insert(data.end(), input.begin(), input.end());
    const std::uint64_t capacity = static_cast<std::uint64_t>(super(planning).slot_sectors) * kSectorSize;
    if (data.size() > capacity) throw std::runtime_error("planned file exceeds slot capacity");
    const int staging = find_free(planning, old_index >= 0 ? static_cast<std::uint32_t>(old_index) : kNoEntry);
    if (staging < 0) throw std::runtime_error("no free COW staging entry");

    std::size_t copied = 0;
    for (std::uint32_t sector = 0; copied < data.size(); ++sector) {
        Write write{};
        write.sector = data_sector(super(planning), static_cast<std::uint32_t>(staging)) + sector;
        const std::size_t chunk = std::min<std::size_t>(kSectorSize, data.size() - copied);
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(copied), chunk, write.bytes.begin());
        apply_full(planning, write);
        writes.push_back(write);
        copied += chunk;
    }

    Entry stage{};
    stage.used = 1U;
    stage.type = kTransaction;
    stage.size = static_cast<std::uint32_t>(data.size());
    stage.checksum = zenov_audit_host::fnv1a(data.data(), data.size());
    stage.reserved = old_index >= 0 ? static_cast<std::uint32_t>(old_index) : kNoEntry;
    set_path(stage, path);
    set_entry_write(planning, writes, static_cast<std::uint32_t>(staging), stage);

    stage.type = kFile;
    stage.flags = kCommitted;
    set_entry_write(planning, writes, static_cast<std::uint32_t>(staging), stage);

    if (old_index >= 0) set_entry_write(planning, writes, static_cast<std::uint32_t>(old_index), Entry{});

    stage.flags = 0U;
    stage.reserved = 0U;
    set_entry_write(planning, writes, static_cast<std::uint32_t>(staging), stage);
}

void persist_file_plan(std::vector<std::uint8_t>& planning, std::vector<Write>& writes,
    const std::string& path, const std::vector<std::uint8_t>& data, bool append = false) {
    write_file_plan(planning, writes, path, data, append);
    sync_metadata_plan(planning, writes);
}

void rename_plan(std::vector<std::uint8_t>& planning, std::vector<Write>& writes,
    const std::string& source, const std::string& destination) {
    if (find_entry(planning, destination) >= 0) throw std::runtime_error("rename destination already exists");
    const int found = find_entry(planning, source);
    if (found < 0) throw std::runtime_error("rename source missing");
    Entry entry = entries(planning)[static_cast<std::uint32_t>(found)];
    set_path(entry, destination);
    set_entry_write(planning, writes, static_cast<std::uint32_t>(found), entry);
    sync_metadata_plan(planning, writes);
}

void remove_plan(std::vector<std::uint8_t>& planning, std::vector<Write>& writes, const std::string& path) {
    const int found = find_entry(planning, path);
    if (found < 0) throw std::runtime_error("remove target missing");
    set_entry_write(planning, writes, static_cast<std::uint32_t>(found), Entry{});
    sync_metadata_plan(planning, writes);
}

std::uint32_t journal_checksum(const TransportJournal& input) {
    TransportJournal copy = input;
    copy.checksum = 0U;
    return zenov_audit_host::fnv1a(reinterpret_cast<const std::uint8_t*>(&copy), sizeof(copy));
}

TransportJournal make_journal(const std::vector<std::uint8_t>& source,
    const std::string& partial_path, std::uint32_t generation, std::uint32_t phase, std::uint32_t offset) {
    TransportJournal journal{};
    const char magic[8] = {'Z','P','T','R','N','1',0,0};
    std::memcpy(journal.magic, magic, sizeof(magic));
    journal.schema = 1U;
    journal.generation = generation;
    journal.phase = phase;
    journal.offset = offset;
    journal.expected_length = static_cast<std::uint32_t>(source.size());
    journal.provider = 1U;
    std::strcpy(journal.name, kPackageName);
    std::strcpy(journal.version, kPackageVersion);
    std::strcpy(journal.source_path, kJournalSourcePath);
    if (partial_path.size() >= sizeof(journal.partial_path)) throw std::runtime_error("partial path too long");
    std::memcpy(journal.partial_path, partial_path.c_str(), partial_path.size() + 1U);
    const auto package_digest = zenov_audit_host::sha256(source.data(), source.size());
    const auto prefix_digest = zenov_audit_host::sha256(source.data(), offset);
    std::memcpy(journal.package_digest, package_digest.data(), package_digest.size());
    std::memcpy(journal.prefix_digest, prefix_digest.data(), prefix_digest.size());
    journal.checksum = journal_checksum(journal);
    return journal;
}

std::vector<std::uint8_t> journal_bytes(const TransportJournal& journal) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&journal);
    return std::vector<std::uint8_t>(begin, begin + sizeof(journal));
}

std::string cache_path(const std::array<std::uint8_t, 32>& digest, bool partial) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string path = "/var/cache/zp/";
    for (std::size_t i = 0; i < 12U; ++i) {
        path.push_back(hex[digest[i] >> 4U]);
        path.push_back(hex[digest[i] & 0x0FU]);
    }
    path += partial ? ".part" : ".zpk";
    return path;
}

Plan build_plan(const std::vector<std::uint8_t>& baseline, const std::vector<std::uint8_t>& source,
    const std::string& partial_path, const std::string& final_path) {
    Plan plan{};
    std::vector<std::uint8_t> planning = baseline;

    persist_file_plan(planning, plan.writes, kJournalPath,
        journal_bytes(make_journal(source, partial_path, 1U, 1U, 0U)));

    const std::size_t first = std::min<std::size_t>(512U, source.size());
    persist_file_plan(planning, plan.writes, partial_path,
        std::vector<std::uint8_t>(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(first)));

    persist_file_plan(planning, plan.writes, kJournalPath,
        journal_bytes(make_journal(source, partial_path, 2U, 1U, static_cast<std::uint32_t>(first))));
    plan.resumable_prefix = plan.writes.size();

    if (first < source.size()) {
        persist_file_plan(planning, plan.writes, partial_path,
            std::vector<std::uint8_t>(source.begin() + static_cast<std::ptrdiff_t>(first), source.end()), true);
    }

    persist_file_plan(planning, plan.writes, kJournalPath,
        journal_bytes(make_journal(source, partial_path, 3U, 1U, static_cast<std::uint32_t>(source.size()))));
    persist_file_plan(planning, plan.writes, kJournalPath,
        journal_bytes(make_journal(source, partial_path, 4U, 2U, static_cast<std::uint32_t>(source.size()))));

    rename_plan(planning, plan.writes, partial_path, final_path);
    plan.commit_recovery_prefix = plan.writes.size();
    remove_plan(planning, plan.writes, kJournalPath);
    return plan;
}

void recover(std::vector<std::uint8_t>& disk) {
    if (disk.size() < kSectorSize) return;
    Superblock& block = super(disk);
    if (!block.entry_count || block.entry_count > kEntryLimit) return;
    Entry* table = entries(disk);
    for (std::uint32_t i = 0; i < block.entry_count; ++i)
        if (table[i].used && table[i].type == kTransaction) table[i] = Entry{};
    for (std::uint32_t i = 0; i < block.entry_count; ++i) {
        Entry& entry = table[i];
        if (!entry.used || entry.type != kFile || !(entry.flags & kCommitted)) continue;
        if (entry.reserved < block.entry_count && entry.reserved != i) table[entry.reserved] = Entry{};
        entry.flags = 0U;
        entry.reserved = 0U;
    }
}

bool valid_tree(const std::vector<std::uint8_t>& disk) {
    try {
        validate_geometry(disk);
        const Superblock& block = super(disk);
        const Entry* table = entries(disk);
        std::vector<std::string> paths;
        paths.reserve(block.entry_count);
        for (std::uint32_t i = 0; i < block.entry_count; ++i) {
            const Entry& entry = table[i];
            if (!entry.used) continue;
            if ((entry.type != kFile && entry.type != kDirectory) || entry.flags) return false;
            const std::string path = entry_path(entry);
            if (path.size() < 2U || path.front() != '/' || path.back() == '/' || path.find("//") != std::string::npos)
                return false;
            if (std::find(paths.begin(), paths.end(), path) != paths.end()) return false;
            paths.push_back(path);
            if (entry.type == kDirectory) {
                if (entry.size || entry.checksum) return false;
                continue;
            }
            if (entry.size > block.slot_sectors * kSectorSize) return false;
            const std::size_t offset = static_cast<std::size_t>(data_sector(block, i)) * kSectorSize;
            if (offset + entry.size > disk.size()) return false;
            if (zenov_audit_host::fnv1a(disk.data() + offset, entry.size) != entry.checksum) return false;
        }
        for (const std::string& path : paths) {
            const auto slash = path.find_last_of('/');
            if (slash == 0U || slash == std::string::npos) continue;
            const std::string parent = path.substr(0, slash);
            const int parent_index = find_entry(disk, parent);
            if (parent_index < 0 || entries(disk)[static_cast<std::uint32_t>(parent_index)].type != kDirectory)
                return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

enum class Outcome { old_state, resumable_state, new_state, fail_closed };

Outcome classify(std::vector<std::uint8_t> disk, const std::vector<std::uint8_t>& expected_source,
    const std::string& partial_path, const std::string& final_path) {
    try {
        validate_geometry(disk);
        recover(disk);
        if (!valid_tree(disk)) return Outcome::fail_closed;
        if (read_file(disk, kSourcePath) != expected_source) return Outcome::fail_closed;

        const int final_index = find_entry(disk, final_path);
        const int partial_index = find_entry(disk, partial_path);
        if (final_index >= 0) {
            if (partial_index >= 0 || read_file(disk, final_path) != expected_source) return Outcome::fail_closed;
            return Outcome::new_state;
        }

        if (partial_index >= 0) {
            const auto partial = read_file(disk, partial_path);
            if (partial.size() > expected_source.size() ||
                !std::equal(partial.begin(), partial.end(), expected_source.begin())) return Outcome::fail_closed;
            return Outcome::resumable_state;
        }

        return Outcome::old_state;
    } catch (...) {
        return Outcome::fail_closed;
    }
}

struct Counts {
    std::uint64_t old_state = 0, resumable_state = 0, new_state = 0, fail_closed = 0, cases = 0;
};

void observe(Counts& counts, Outcome outcome) {
    ++counts.cases;
    if (outcome == Outcome::old_state) ++counts.old_state;
    else if (outcome == Outcome::resumable_state) ++counts.resumable_state;
    else if (outcome == Outcome::new_state) ++counts.new_state;
    else ++counts.fail_closed;
}

Counts run_matrix(const std::vector<std::uint8_t>& baseline, const Plan& plan,
    const std::vector<std::uint8_t>& source, const std::string& partial_path, const std::string& final_path) {
    Counts counts{};
    bool saw_old = false, saw_resumable = false, saw_new = false;

    for (std::size_t prefix = 0; prefix <= plan.writes.size(); ++prefix) {
        auto crashed = baseline;
        for (std::size_t i = 0; i < prefix; ++i) apply_full(crashed, plan.writes[i]);
        const Outcome outcome = classify(crashed, source, partial_path, final_path);
        if (outcome == Outcome::fail_closed)
            throw std::runtime_error("ordered crash prefix failed closed: " + std::to_string(prefix));
        saw_old = saw_old || outcome == Outcome::old_state;
        saw_resumable = saw_resumable || outcome == Outcome::resumable_state;
        saw_new = saw_new || outcome == Outcome::new_state;
        observe(counts, outcome);
    }
    if (!saw_old || !saw_resumable || !saw_new)
        throw std::runtime_error("ordered prefixes did not expose old, resumable and new states");

    constexpr std::array<std::size_t, 7> cuts = {1U,64U,128U,255U,256U,384U,511U};
    for (std::size_t index = 0; index < plan.writes.size(); ++index) {
        for (std::size_t cut : cuts) {
            for (bool suffix : {false, true}) {
                auto crashed = baseline;
                for (std::size_t i = 0; i < index; ++i) apply_full(crashed, plan.writes[i]);
                apply_torn(crashed, plan.writes[index], cut, suffix);
                observe(counts, classify(crashed, source, partial_path, final_path));
            }
        }
    }

    for (std::size_t index = 0; index < plan.writes.size(); ++index) {
        auto crashed = baseline;
        for (std::size_t i = 0; i < index; ++i) apply_full(crashed, plan.writes[i]);
        apply_garbage(crashed, plan.writes[index], static_cast<std::uint32_t>(index + 1U));
        observe(counts, classify(crashed, source, partial_path, final_path));
    }

    for (std::size_t dropped = 0; dropped < plan.writes.size(); ++dropped) {
        auto crashed = baseline;
        for (std::size_t i = 0; i < plan.writes.size(); ++i)
            if (i != dropped) apply_full(crashed, plan.writes[i]);
        observe(counts, classify(crashed, source, partial_path, final_path));
    }

    for (std::size_t duplicate = 0; duplicate < plan.writes.size(); ++duplicate) {
        auto crashed = baseline;
        for (std::size_t i = 0; i < plan.writes.size(); ++i) {
            apply_full(crashed, plan.writes[i]);
            if (i == duplicate) apply_full(crashed, plan.writes[i]);
        }
        const Outcome outcome = classify(crashed, source, partial_path, final_path);
        if (outcome != Outcome::new_state)
            throw std::runtime_error("duplicate write was not idempotent: " + std::to_string(duplicate));
        observe(counts, outcome);
    }

    for (std::size_t swapped = 0; swapped + 1U < plan.writes.size(); ++swapped) {
        auto crashed = baseline;
        for (std::size_t i = 0; i < plan.writes.size(); ++i) {
            if (i == swapped) apply_full(crashed, plan.writes[i + 1U]);
            else if (i == swapped + 1U) apply_full(crashed, plan.writes[i - 1U]);
            else apply_full(crashed, plan.writes[i]);
        }
        observe(counts, classify(crashed, source, partial_path, final_path));
    }

    if (!counts.fail_closed) throw std::runtime_error("destructive matrix never exercised fail-closed behavior");
    std::cout << "ZENPKG_TRANSPORT_COW_FAULT_MATRIX_OK cases=" << counts.cases
              << " writes=" << plan.writes.size()
              << " old=" << counts.old_state
              << " resumable=" << counts.resumable_state
              << " new=" << counts.new_state
              << " fail_closed=" << counts.fail_closed << "\n";
    std::cout << "ZENPKG_TRANSPORT_OLD_OR_RESUMABLE_OR_NEW_OR_FAIL_CLOSED_ONLY\n";
    return counts;
}

std::vector<std::uint8_t> apply_prefix(const std::vector<std::uint8_t>& baseline,
    const Plan& plan, std::size_t prefix) {
    if (prefix > plan.writes.size()) throw std::runtime_error("invalid emit prefix");
    auto image = baseline;
    for (std::size_t i = 0; i < prefix; ++i) apply_full(image, plan.writes[i]);
    return image;
}

void corrupt_journal_payload(std::vector<std::uint8_t>& image) {
    const int index = find_entry(image, kJournalPath);
    if (index < 0) throw std::runtime_error("journal missing from emitted image");
    const std::size_t offset = static_cast<std::size_t>(
        data_sector(super(image), static_cast<std::uint32_t>(index))) * kSectorSize;
    image[offset + 40U] ^= 0x40U;
}

void corrupt_final_payload(std::vector<std::uint8_t>& image, const std::string& final_path) {
    const int index = find_entry(image, final_path);
    if (index < 0) throw std::runtime_error("final cache object missing");
    Entry& entry = entries(image)[static_cast<std::uint32_t>(index)];
    if (entry.size < 128U) throw std::runtime_error("final cache object too small");
    const std::size_t offset = static_cast<std::size_t>(
        data_sector(super(image), static_cast<std::uint32_t>(index))) * kSectorSize;
    image[offset + 96U] ^= 0x01U;
    entry.checksum = zenov_audit_host::fnv1a(image.data() + offset, entry.size);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "usage: zenpkg-transport-fault-test <zenov-data.img>"
                      << " [--emit-invalid-journal <img>]"
                      << " [--emit-commit-recovery <img>]"
                      << " [--emit-corrupt-final <img>]\n";
            return 2;
        }
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");

        std::string invalid_journal_path, commit_recovery_path, corrupt_final_path;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--emit-invalid-journal" && i + 1 < argc) invalid_journal_path = argv[++i];
            else if (argument == "--emit-commit-recovery" && i + 1 < argc) commit_recovery_path = argv[++i];
            else if (argument == "--emit-corrupt-final" && i + 1 < argc) corrupt_final_path = argv[++i];
            else throw std::runtime_error("unknown or incomplete argument: " + argument);
        }

        auto baseline = read_image(argv[1]);
        validate_geometry(baseline);
        add_directory_baseline(baseline, "/var");
        add_directory_baseline(baseline, "/var/lib");
        add_directory_baseline(baseline, "/var/lib/zenpkg");
        add_directory_baseline(baseline, "/var/cache");
        add_directory_baseline(baseline, "/var/cache/zp");

        const auto source = read_file(baseline, kSourcePath);
        if (source.size() <= 512U || source.size() > 64U * 1024U)
            throw std::runtime_error("signed package size does not exercise two chunks");
        const auto digest = zenov_audit_host::sha256(source.data(), source.size());
        const std::string partial_path = cache_path(digest, true);
        const std::string final_path = cache_path(digest, false);
        clear_baseline_path(baseline, kJournalPath);
        clear_baseline_path(baseline, partial_path);
        clear_baseline_path(baseline, final_path);

        const Plan plan = build_plan(baseline, source, partial_path, final_path);
        const Counts counts = run_matrix(baseline, plan, source, partial_path, final_path);
        if (counts.cases < 1000U) throw std::runtime_error("fault matrix unexpectedly small");

        if (!invalid_journal_path.empty()) {
            auto image = apply_prefix(baseline, plan, plan.resumable_prefix);
            corrupt_journal_payload(image);
            write_image(invalid_journal_path, image);
            std::cout << "ZENPKG_TRANSPORT_INVALID_JOURNAL_IMAGE_OK output=" << invalid_journal_path << "\n";
        }
        if (!commit_recovery_path.empty()) {
            auto image = apply_prefix(baseline, plan, plan.commit_recovery_prefix);
            write_image(commit_recovery_path, image);
            std::cout << "ZENPKG_TRANSPORT_COMMIT_RECOVERY_IMAGE_OK output=" << commit_recovery_path << "\n";
        }
        if (!corrupt_final_path.empty()) {
            auto image = apply_prefix(baseline, plan, plan.commit_recovery_prefix);
            corrupt_final_payload(image, final_path);
            write_image(corrupt_final_path, image);
            std::cout << "ZENPKG_TRANSPORT_CORRUPT_FINAL_IMAGE_OK output=" << corrupt_final_path << "\n";
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenpkg-transport-fault-test: " << error.what() << "\n";
        return 1;
    }
}
