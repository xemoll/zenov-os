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

#include "zenov_audit_format.hpp"

namespace {
using zenov_audit_host::Journal;
using zenov_audit_host::Record;
using zenov_audit_host::Header;

constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kEntryLimit = 128U;
constexpr std::uint8_t kFile = 1U;
constexpr std::uint8_t kTransaction = 3U;
constexpr std::uint16_t kCommitted = 1U;
constexpr char kAuditPath[] = "/security/zenovguard.audit";

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
static_assert(sizeof(Superblock) == kSectorSize);
static_assert(sizeof(Entry) == 64U);

std::array<std::uint8_t, 32> event_digest(std::uint32_t sequence) {
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) digest[i] = static_cast<std::uint8_t>((sequence * 37U + i * 13U) & 0xFFU);
    return digest;
}
std::string event_path(std::uint32_t sequence) { return "/fault/audit-" + std::to_string(sequence); }
void append_event(Journal& journal, std::uint32_t tick) {
    Header& header = journal.header;
    if (header.next_sequence == 0xFFFFFFFFU) throw std::runtime_error("audit sequence exhausted");
    const std::uint32_t index = header.next_index;
    if (header.count == zenov_audit_host::kCapacity) std::memcpy(header.anchor_hash, journal.records[index].record_hash, 32U);
    Record& record = journal.records[index];
    std::memset(&record, 0, sizeof(record));
    record.sequence = header.next_sequence;
    record.tick = tick;
    record.action = 2U;
    record.verdict = 1U;
    const std::string path = event_path(record.sequence);
    if (path.size() + 1U > sizeof(record.path)) throw std::runtime_error("fault path too long");
    std::memcpy(record.path, path.c_str(), path.size() + 1U);
    record.path_length = static_cast<std::uint16_t>(path.size());
    const auto digest = event_digest(record.sequence);
    std::memcpy(record.digest, digest.data(), digest.size());
    const auto hash = zenov_audit_host::record_hash(record, header.head_hash);
    std::memcpy(record.record_hash, hash.data(), hash.size());
    if (header.count < zenov_audit_host::kCapacity) ++header.count;
    header.next_index = (index + 1U) % zenov_audit_host::kCapacity;
    ++header.next_sequence;
    std::memcpy(header.head_hash, record.record_hash, 32U);
    if (!zenov_audit_host::verify(journal)) throw std::runtime_error("generated audit journal is invalid");
}
void verify_known_record_vector() {
    Journal sample{};
    zenov_audit_host::initialize_empty(sample);
    append_event(sample, 100U);
    static constexpr std::uint8_t expected[32] = {
        0xe1,0x81,0xea,0x2d,0x93,0xd0,0x54,0x9e,0xe2,0x29,0x61,0x4c,0x87,0x34,0x53,0xaa,
        0xbf,0x1c,0x46,0x71,0x8d,0xc5,0x5e,0xde,0x90,0xee,0x75,0x88,0x64,0xaa,0x45,0x60,
    };
    if (!zenov_audit_host::same_hash(sample.records[0].record_hash, expected)) throw std::runtime_error("ZGAL1 known record hash failed");
}

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
Superblock& super(std::vector<std::uint8_t>& disk) { return *reinterpret_cast<Superblock*>(disk.data()); }
const Superblock& super(const std::vector<std::uint8_t>& disk) { return *reinterpret_cast<const Superblock*>(disk.data()); }
Entry* entries(std::vector<std::uint8_t>& disk) { return reinterpret_cast<Entry*>(disk.data() + kSectorSize); }
const Entry* entries(const std::vector<std::uint8_t>& disk) { return reinterpret_cast<const Entry*>(disk.data() + kSectorSize); }
std::uint32_t metadata_sector(std::uint32_t index) { return 1U + index / 8U; }
std::uint32_t slot_offset(std::uint32_t index) { return (index % 8U) * sizeof(Entry); }
std::uint32_t data_sector(const Superblock& block, std::uint32_t index) { return block.data_start + index * block.slot_sectors; }
bool path_equal(const Entry& entry, const char* path) {
    const std::size_t length = std::strlen(path);
    return length < sizeof(entry.path) && std::memcmp(entry.path, path, length) == 0 && entry.path[length] == 0;
}
void validate_image(const std::vector<std::uint8_t>& disk) {
    if (disk.size() < kSectorSize || std::memcmp(disk.data(), "ZENOVFS1", 8U) != 0) throw std::runtime_error("not a ZenovFS1 image");
    const Superblock& block = super(disk);
    if (!block.entry_count || block.entry_count > kEntryLimit || !block.slot_sectors ||
        disk.size() < static_cast<std::size_t>(block.total_sectors) * kSectorSize) throw std::runtime_error("invalid ZenovFS geometry");
}
int find_audit(const std::vector<std::uint8_t>& disk) {
    const Superblock& block = super(disk);
    const Entry* table = entries(disk);
    int found = -1;
    for (std::uint32_t i = 0; i < block.entry_count; ++i) {
        if (table[i].used && table[i].type == kFile && path_equal(table[i], kAuditPath)) {
            if (found >= 0) throw std::runtime_error("duplicate audit path in source image");
            found = static_cast<int>(i);
        }
    }
    if (found < 0) throw std::runtime_error("audit path missing from source image");
    return found;
}
int find_free(const std::vector<std::uint8_t>& disk, std::uint32_t excluded) {
    const Superblock& block = super(disk);
    const Entry* table = entries(disk);
    for (std::uint32_t i = 0; i < block.entry_count; ++i) if (i != excluded && !table[i].used) return static_cast<int>(i);
    return -1;
}
Journal read_audit(const std::vector<std::uint8_t>& disk, std::uint32_t index) {
    const Superblock& block = super(disk);
    const Entry& entry = entries(disk)[index];
    if (!entry.used || entry.type != kFile || entry.flags || entry.size != sizeof(Journal) || !path_equal(entry, kAuditPath)) throw std::runtime_error("invalid audit metadata");
    const std::size_t offset = static_cast<std::size_t>(data_sector(block, index)) * kSectorSize;
    if (offset + sizeof(Journal) > disk.size()) throw std::runtime_error("audit payload outside image");
    Journal journal{};
    std::memcpy(&journal, disk.data() + offset, sizeof(journal));
    if (zenov_audit_host::fnv1a(reinterpret_cast<const std::uint8_t*>(&journal), sizeof(journal)) != entry.checksum || !zenov_audit_host::verify(journal)) throw std::runtime_error("invalid source audit journal");
    return journal;
}
void patch_audit(std::vector<std::uint8_t>& disk, std::uint32_t index, const Journal& journal) {
    Entry& entry = entries(disk)[index];
    const std::size_t offset = static_cast<std::size_t>(data_sector(super(disk), index)) * kSectorSize;
    std::memcpy(disk.data() + offset, &journal, sizeof(journal));
    entry.size = sizeof(journal);
    entry.checksum = zenov_audit_host::fnv1a(reinterpret_cast<const std::uint8_t*>(&journal), sizeof(journal));
    entry.flags = 0;
    entry.reserved = 0;
}

struct Write { std::uint32_t sector = 0; std::array<std::uint8_t, kSectorSize> bytes{}; };
void apply_full(std::vector<std::uint8_t>& disk, const Write& write) {
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (offset + kSectorSize > disk.size()) throw std::runtime_error("write outside image");
    std::copy(write.bytes.begin(), write.bytes.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
}
void apply_torn(std::vector<std::uint8_t>& disk, const Write& write, std::size_t count, bool suffix) {
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (!count || count >= kSectorSize || offset + kSectorSize > disk.size()) throw std::runtime_error("invalid torn write");
    const std::size_t start = suffix ? kSectorSize - count : 0U;
    std::copy_n(write.bytes.begin() + static_cast<std::ptrdiff_t>(start), count, disk.begin() + static_cast<std::ptrdiff_t>(offset + start));
}
void apply_garbage(std::vector<std::uint8_t>& disk, const Write& write, std::uint32_t seed) {
    const std::size_t offset = static_cast<std::size_t>(write.sector) * kSectorSize;
    if (offset + kSectorSize > disk.size()) throw std::runtime_error("garbage write outside image");
    for (std::size_t i = 0; i < kSectorSize; ++i) disk[offset + i] = static_cast<std::uint8_t>((seed * 29U + i * 131U + 0x5AU) & 0xFFU);
}
Write sector_snapshot(const std::vector<std::uint8_t>& disk, std::uint32_t sector) {
    Write write{};
    write.sector = sector;
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
void clear_entry_write(std::vector<std::uint8_t>& planning, std::vector<Write>& writes, std::uint32_t index) { set_entry_write(planning, writes, index, Entry{}); }

struct Plan { std::vector<Write> writes; std::size_t payload_writes = 0; std::uint32_t old_index = 0, staging_index = 0; };
Plan plan_replacement(const std::vector<std::uint8_t>& base, const Journal& next) {
    Plan plan{};
    plan.old_index = static_cast<std::uint32_t>(find_audit(base));
    const int free = find_free(base, plan.old_index);
    if (free < 0) throw std::runtime_error("no free staging slot");
    plan.staging_index = static_cast<std::uint32_t>(free);
    std::vector<std::uint8_t> planning = base;
    const auto* data = reinterpret_cast<const std::uint8_t*>(&next);
    std::size_t copied = 0;
    for (std::uint32_t sector = 0; copied < sizeof(next); ++sector) {
        Write write{};
        write.sector = data_sector(super(base), plan.staging_index) + sector;
        const std::size_t chunk = std::min<std::size_t>(kSectorSize, sizeof(next) - copied);
        std::copy_n(data + copied, chunk, write.bytes.begin());
        apply_full(planning, write);
        plan.writes.push_back(write);
        copied += chunk;
    }
    plan.payload_writes = plan.writes.size();
    Entry stage{};
    stage.used = 1U;
    stage.type = kTransaction;
    stage.size = sizeof(next);
    stage.checksum = zenov_audit_host::fnv1a(reinterpret_cast<const std::uint8_t*>(&next), sizeof(next));
    stage.reserved = plan.old_index;
    std::memcpy(stage.path, kAuditPath, sizeof(kAuditPath));
    set_entry_write(planning, plan.writes, plan.staging_index, stage);
    stage.type = kFile;
    stage.flags = kCommitted;
    set_entry_write(planning, plan.writes, plan.staging_index, stage);
    clear_entry_write(planning, plan.writes, plan.old_index);
    stage.flags = 0;
    stage.reserved = 0;
    set_entry_write(planning, plan.writes, plan.staging_index, stage);
    return plan;
}
void recover(std::vector<std::uint8_t>& disk) {
    Entry* table = entries(disk);
    const Superblock& block = super(disk);
    for (std::uint32_t i = 0; i < block.entry_count; ++i) if (table[i].used && table[i].type == kTransaction) table[i] = Entry{};
    for (std::uint32_t i = 0; i < block.entry_count; ++i) {
        Entry& item = table[i];
        if (!item.used || item.type != kFile || !(item.flags & kCommitted)) continue;
        if (item.reserved < block.entry_count && item.reserved != i) table[item.reserved] = Entry{};
        item.flags = 0;
        item.reserved = 0;
    }
}

enum class Outcome { old_state, new_state, fail_closed };
Outcome classify(std::vector<std::uint8_t> disk, const Journal& old_journal, const Journal& new_journal) {
    recover(disk);
    const Superblock& block = super(disk);
    const Entry* table = entries(disk);
    int found = -1;
    for (std::uint32_t i = 0; i < block.entry_count; ++i) {
        if (table[i].used && table[i].type == kFile && path_equal(table[i], kAuditPath)) {
            if (found >= 0) return Outcome::fail_closed;
            found = static_cast<int>(i);
        }
    }
    if (found < 0) return Outcome::fail_closed;
    const Entry& entry = table[static_cast<std::uint32_t>(found)];
    if (entry.flags || entry.size != sizeof(Journal)) return Outcome::fail_closed;
    const std::size_t offset = static_cast<std::size_t>(data_sector(block, static_cast<std::uint32_t>(found))) * kSectorSize;
    if (offset + sizeof(Journal) > disk.size()) return Outcome::fail_closed;
    Journal candidate{};
    std::memcpy(&candidate, disk.data() + offset, sizeof(candidate));
    if (zenov_audit_host::fnv1a(reinterpret_cast<const std::uint8_t*>(&candidate), sizeof(candidate)) != entry.checksum || !zenov_audit_host::verify(candidate)) return Outcome::fail_closed;
    if (std::memcmp(&candidate, &old_journal, sizeof(candidate)) == 0) return Outcome::old_state;
    if (std::memcmp(&candidate, &new_journal, sizeof(candidate)) == 0) return Outcome::new_state;
    throw std::runtime_error("fault produced an unexpected but valid audit journal");
}

struct Counts { std::uint64_t old_state = 0, new_state = 0, fail_closed = 0, cases = 0; };
void observe(Counts& counts, Outcome outcome) {
    ++counts.cases;
    if (outcome == Outcome::old_state) ++counts.old_state;
    else if (outcome == Outcome::new_state) ++counts.new_state;
    else ++counts.fail_closed;
}
void require_recoverable(Outcome outcome, const std::string& label) {
    if (outcome == Outcome::fail_closed) throw std::runtime_error(label + " unexpectedly failed closed");
}
Counts run_fault_matrix(const std::string& name, const std::vector<std::uint8_t>& base, const Journal& old_journal, const Journal& new_journal, const Plan& plan) {
    Counts counts{};
    bool saw_old = false, saw_new = false;
    for (std::size_t prefix = 0; prefix <= plan.writes.size(); ++prefix) {
        auto crashed = base;
        for (std::size_t i = 0; i < prefix; ++i) apply_full(crashed, plan.writes[i]);
        const Outcome outcome = classify(crashed, old_journal, new_journal);
        require_recoverable(outcome, name + " ordered prefix " + std::to_string(prefix));
        saw_old = saw_old || outcome == Outcome::old_state;
        saw_new = saw_new || outcome == Outcome::new_state;
        observe(counts, outcome);
    }
    if (!saw_old || !saw_new) throw std::runtime_error(name + " ordered prefixes did not expose both atomic states");

    constexpr std::array<std::size_t, 7> cuts = {1U,64U,128U,255U,256U,384U,511U};
    for (std::size_t index = 0; index < plan.writes.size(); ++index) for (std::size_t cut : cuts) for (bool suffix : {false, true}) {
        auto crashed = base;
        for (std::size_t i = 0; i < index; ++i) apply_full(crashed, plan.writes[i]);
        apply_torn(crashed, plan.writes[index], cut, suffix);
        observe(counts, classify(crashed, old_journal, new_journal));
    }
    for (std::size_t index = 0; index < plan.writes.size(); ++index) {
        auto crashed = base;
        for (std::size_t i = 0; i < index; ++i) apply_full(crashed, plan.writes[i]);
        apply_garbage(crashed, plan.writes[index], static_cast<std::uint32_t>(index + 1U));
        observe(counts, classify(crashed, old_journal, new_journal));
    }
    for (std::size_t dropped = 0; dropped < plan.writes.size(); ++dropped) {
        auto crashed = base;
        for (std::size_t i = 0; i < plan.writes.size(); ++i) if (i != dropped) apply_full(crashed, plan.writes[i]);
        observe(counts, classify(crashed, old_journal, new_journal));
    }
    for (std::size_t duplicate = 0; duplicate < plan.writes.size(); ++duplicate) {
        auto crashed = base;
        for (std::size_t i = 0; i < plan.writes.size(); ++i) { apply_full(crashed, plan.writes[i]); if (i == duplicate) apply_full(crashed, plan.writes[i]); }
        const Outcome outcome = classify(crashed, old_journal, new_journal);
        if (outcome != Outcome::new_state) throw std::runtime_error(name + " duplicate write was not idempotent");
        observe(counts, outcome);
    }
    for (std::size_t swapped = 0; swapped + 1U < plan.writes.size(); ++swapped) {
        auto crashed = base;
        for (std::size_t i = 0; i < plan.writes.size(); ++i) {
            if (i == swapped) apply_full(crashed, plan.writes[i + 1U]);
            else if (i == swapped + 1U) apply_full(crashed, plan.writes[i - 1U]);
            else apply_full(crashed, plan.writes[i]);
        }
        observe(counts, classify(crashed, old_journal, new_journal));
    }
    std::array<std::size_t, 4> metadata = {plan.payload_writes, plan.payload_writes + 1U, plan.payload_writes + 2U, plan.payload_writes + 3U};
    do {
        for (std::size_t payload_prefix = 0; payload_prefix <= plan.payload_writes; ++payload_prefix) {
            auto crashed = base;
            for (std::size_t i = 0; i < payload_prefix; ++i) apply_full(crashed, plan.writes[i]);
            for (std::size_t index : metadata) apply_full(crashed, plan.writes[index]);
            observe(counts, classify(crashed, old_journal, new_journal));
        }
    } while (std::next_permutation(metadata.begin(), metadata.end()));

    if (!counts.fail_closed) throw std::runtime_error(name + " destructive matrix never exercised fail-closed behavior");
    std::cout << "ZENOV_AUDIT_COW_FAULT_MATRIX_OK scenario=" << name << " cases=" << counts.cases
              << " old=" << counts.old_state << " new=" << counts.new_state << " fail_closed=" << counts.fail_closed << "\n";
    return counts;
}
void apply_prefix(std::vector<std::uint8_t>& disk, const Plan& plan, std::size_t prefix) {
    if (prefix > plan.writes.size()) throw std::runtime_error("invalid recovery prefix");
    for (std::size_t i = 0; i < prefix; ++i) apply_full(disk, plan.writes[i]);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "usage: zenovfs-audit-fault-test <zenov-data.img> [--emit-old-recovery <img>] [--emit-new-recovery <img>] [--emit-corrupt <img>]\n";
            return 2;
        }
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 known-answer test failed");
        verify_known_record_vector();
        std::string old_recovery_path, new_recovery_path, corrupt_path;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--emit-old-recovery" && i + 1 < argc) old_recovery_path = argv[++i];
            else if (argument == "--emit-new-recovery" && i + 1 < argc) new_recovery_path = argv[++i];
            else if (argument == "--emit-corrupt" && i + 1 < argc) corrupt_path = argv[++i];
            else throw std::runtime_error("unknown or incomplete argument: " + argument);
        }
        const auto original = read_image(argv[1]);
        validate_image(original);
        const std::uint32_t original_index = static_cast<std::uint32_t>(find_audit(original));
        const Journal empty = read_audit(original, original_index);
        if (empty.header.count != 0U) throw std::runtime_error("factory audit journal must be empty");

        Journal one = empty;
        append_event(one, 100U);
        const Plan empty_plan = plan_replacement(original, one);
        const Counts empty_counts = run_fault_matrix("empty-to-one", original, empty, one, empty_plan);

        auto full_base = original;
        Journal full{};
        zenov_audit_host::initialize_empty(full);
        for (std::uint32_t i = 0; i < zenov_audit_host::kCapacity; ++i) append_event(full, 1000U + i);
        patch_audit(full_base, original_index, full);
        Journal rotated = full;
        append_event(rotated, 2000U);
        const Plan rotation_plan = plan_replacement(full_base, rotated);
        const Counts rotation_counts = run_fault_matrix("full-ring-rotation", full_base, full, rotated, rotation_plan);

        if (!old_recovery_path.empty()) {
            auto image = original;
            apply_prefix(image, empty_plan, empty_plan.payload_writes + 1U);
            write_image(old_recovery_path, image);
            std::cout << "ZENOV_AUDIT_OLD_RECOVERY_IMAGE_OK output=" << old_recovery_path << "\n";
        }
        if (!new_recovery_path.empty()) {
            auto image = original;
            apply_prefix(image, empty_plan, empty_plan.payload_writes + 2U);
            write_image(new_recovery_path, image);
            std::cout << "ZENOV_AUDIT_NEW_RECOVERY_IMAGE_OK output=" << new_recovery_path << "\n";
        }
        if (!corrupt_path.empty()) {
            auto image = original;
            for (std::size_t i = 0; i < empty_plan.writes.size(); ++i) if (i != 0U) apply_full(image, empty_plan.writes[i]);
            write_image(corrupt_path, image);
            std::cout << "ZENOV_AUDIT_FAIL_CLOSED_IMAGE_OK output=" << corrupt_path << "\n";
        }
        const std::uint64_t total = empty_counts.cases + rotation_counts.cases;
        std::cout << "ZENOV_AUDIT_COW_FAULT_INJECTION_OK total_cases=" << total << "\n";
        std::cout << "ZENOV_AUDIT_COW_OLD_OR_NEW_OR_FAIL_CLOSED_ONLY\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-audit-fault-test: " << error.what() << "\n";
        return 1;
    }
}
