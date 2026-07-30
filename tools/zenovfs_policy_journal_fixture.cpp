#include "zenpkg/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kEntryCount = 128U;
constexpr std::uint8_t kTypeFile = 1U;
constexpr std::uint8_t kJournalPrepared = 1U;
constexpr std::uint8_t kDomainZmid = 3U;

#pragma pack(push, 1)
struct Superblock {
    char magic[8];
    std::uint32_t version, total_sectors, entry_count, entry_sectors;
    std::uint32_t data_start, slot_sectors, generation;
    char label[16];
    std::uint8_t reserved[460];
};
struct Entry {
    std::uint8_t used, type;
    std::uint16_t flags;
    char path[48];
    std::uint32_t size, checksum, reserved;
};
struct JournalHeader {
    char magic[4];
    std::uint16_t schema;
    std::uint16_t header_size;
    std::uint8_t state;
    std::uint8_t domain;
    std::uint16_t flags;
    std::uint32_t policy_size;
    std::uint32_t version_size;
    std::uint32_t auxiliary_size;
    std::uint32_t previous_version;
    std::uint32_t payload_size;
    std::uint8_t digest[32];
    char live_path[48];
    char version_path[48];
    char auxiliary_path[48];
    std::uint8_t reserved[16];
};
#pragma pack(pop)

static_assert(sizeof(Superblock) == kSectorSize);
static_assert(sizeof(Entry) == 64U);
static_assert(sizeof(JournalHeader) == 224U);

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void copy_path(char output[48], std::string_view path) {
    require(!path.empty() && path.size() < 48U && path.front() == '/', "invalid journal path");
    std::memset(output, 0, 48U);
    std::memcpy(output, path.data(), path.size());
}

struct Image {
    std::vector<std::uint8_t> disk;
    Superblock* super = nullptr;
    Entry* entries = nullptr;

    explicit Image(const std::filesystem::path& path) : disk(zenpkg::read_binary(path)) {
        require(disk.size() >= kSectorSize + kEntryCount * sizeof(Entry), "image is truncated");
        super = reinterpret_cast<Superblock*>(disk.data());
        entries = reinterpret_cast<Entry*>(disk.data() + kSectorSize);
        const char magic[8] = {'Z','E','N','O','V','F','S','1'};
        require(std::memcmp(super->magic, magic, sizeof(magic)) == 0 && super->version == 1U,
            "not a ZenovFS1 image");
        require(super->entry_count == kEntryCount && super->entry_sectors == 16U &&
            super->slot_sectors != 0U, "unsupported ZenovFS layout");
        const std::uint64_t data_end = static_cast<std::uint64_t>(super->data_start) +
            static_cast<std::uint64_t>(super->entry_count) * super->slot_sectors;
        require(data_end <= super->total_sectors &&
            static_cast<std::uint64_t>(super->total_sectors) * kSectorSize <= disk.size(),
            "ZenovFS layout exceeds image");
    }

    std::uint32_t max_file_bytes() const { return super->slot_sectors * kSectorSize; }

    std::size_t slot_offset(std::uint32_t index) const {
        require(index < super->entry_count, "entry index out of range");
        return static_cast<std::size_t>(super->data_start + index * super->slot_sectors) * kSectorSize;
    }

    std::uint32_t find(std::string_view path) const {
        for (std::uint32_t i = 0U; i < super->entry_count; ++i) {
            const Entry& entry = entries[i];
            if (!entry.used || entry.type != kTypeFile) continue;
            const std::size_t length = std::strnlen(entry.path, sizeof(entry.path));
            if (length == path.size() && std::memcmp(entry.path, path.data(), length) == 0) return i;
        }
        throw std::runtime_error("missing ZenovFS file: " + std::string(path));
    }

    std::vector<std::uint8_t> read(std::string_view path) const {
        const std::uint32_t index = find(path);
        const Entry& entry = entries[index];
        require(entry.size <= max_file_bytes(), "file exceeds slot capacity");
        const std::size_t offset = slot_offset(index);
        require(offset + entry.size <= disk.size(), "file data outside image");
        std::vector<std::uint8_t> data(entry.size);
        if (!data.empty()) std::memcpy(data.data(), disk.data() + offset, data.size());
        require(fnv1a(data.data(), data.size()) == entry.checksum,
            "ZenovFS checksum mismatch: " + std::string(path));
        return data;
    }

    void write(std::string_view path, const std::vector<std::uint8_t>& data) {
        const std::uint32_t index = find(path);
        require(data.size() <= max_file_bytes(), "replacement exceeds slot capacity");
        const std::size_t offset = slot_offset(index);
        require(offset + max_file_bytes() <= disk.size(), "slot outside image");
        std::memset(disk.data() + offset, 0, max_file_bytes());
        if (!data.empty()) std::memcpy(disk.data() + offset, data.data(), data.size());
        Entry& entry = entries[index];
        entry.used = 1U;
        entry.type = kTypeFile;
        entry.flags = 0U;
        entry.size = static_cast<std::uint32_t>(data.size());
        entry.checksum = fnv1a(data.data(), data.size());
        entry.reserved = 0U;
    }

    void save(const std::filesystem::path& path) {
        require(super->generation != 0xFFFFFFFFU, "generation exhausted");
        ++super->generation;
        zenpkg::write_binary_atomic(path, disk);
    }
};

std::uint32_t parse_version(const std::vector<std::uint8_t>& data) {
    require(data.size() >= 2U && data.size() <= 11U && data.back() == '\n', "invalid version state");
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i + 1U < data.size(); ++i) {
        require(data[i] >= '0' && data[i] <= '9', "non-decimal version state");
        const std::uint32_t digit = data[i] - '0';
        require(value <= (0xFFFFFFFFU - digit) / 10U, "version overflow");
        value = value * 10U + digit;
    }
    require(value != 0U, "zero version state");
    return value;
}

std::vector<std::uint8_t> build_hot_zmid_journal(const std::vector<std::uint8_t>& policy,
        const std::vector<std::uint8_t>& version, const std::vector<std::uint8_t>& audit) {
    require(!policy.empty() && !version.empty() && !audit.empty(), "journal backup is empty");
    const std::uint32_t previous_version = parse_version(version);
    const std::size_t total = sizeof(JournalHeader) + policy.size() + version.size() + audit.size();
    std::vector<std::uint8_t> journal(total, 0U);
    auto* header = reinterpret_cast<JournalHeader*>(journal.data());
    header->magic[0] = 'Z'; header->magic[1] = 'P'; header->magic[2] = 'T'; header->magic[3] = 'J';
    header->schema = 1U;
    header->header_size = sizeof(JournalHeader);
    header->state = kJournalPrepared;
    header->domain = kDomainZmid;
    header->policy_size = static_cast<std::uint32_t>(policy.size());
    header->version_size = static_cast<std::uint32_t>(version.size());
    header->auxiliary_size = static_cast<std::uint32_t>(audit.size());
    header->previous_version = previous_version;
    header->payload_size = header->policy_size + header->version_size + header->auxiliary_size;
    copy_path(header->live_path, "/security/zenovguard-intelligence.zmid");
    copy_path(header->version_path, "/security/zenovguard-intelligence.version");
    copy_path(header->auxiliary_path, "/security/zenovguard.audit");

    std::size_t offset = sizeof(JournalHeader);
    std::memcpy(journal.data() + offset, policy.data(), policy.size());
    offset += policy.size();
    std::memcpy(journal.data() + offset, version.data(), version.size());
    offset += version.size();
    std::memcpy(journal.data() + offset, audit.data(), audit.size());

    const auto digest = zenpkg::Sha256::hash(journal);
    std::memcpy(header->digest, digest.data(), digest.size());
    return journal;
}

void make_hot_zmid(const std::filesystem::path& input, const std::filesystem::path& output) {
    Image image(input);
    const auto old_policy = image.read("/security/zenovguard-intelligence.zmid");
    const auto old_version = image.read("/security/zenovguard-intelligence.version");
    const auto old_audit = image.read("/security/zenovguard.audit");
    const auto new_policy = image.read("/security/updates/zmid-v2.zmid");
    require(parse_version(old_version) == 1U, "fixture expects ZMID version 1");

    const auto journal = build_hot_zmid_journal(old_policy, old_version, old_audit);
    image.write("/security/policy-transaction.journal", journal);
    image.write("/security/zenovguard-intelligence.zmid", new_policy);
    image.write("/security/zenovguard-intelligence.version", {'2','\n'});

    auto damaged_audit = old_audit;
    require(damaged_audit.size() >= 4U, "audit fixture is too small");
    damaged_audit[0] ^= 0x01U;
    image.write("/security/zenovguard.audit", damaged_audit);
    image.save(output);
    std::cout << "POLICY_JOURNAL_FIXTURE_OK mode=hot-zmid previous=1 live=2 audit=damaged-checksummed journal_bytes="
              << journal.size() << '\n';
}

void make_corrupt_journal(const std::filesystem::path& input, const std::filesystem::path& output) {
    Image image(input);
    auto journal = image.read("/security/policy-transaction.journal");
    require(journal.size() > sizeof(JournalHeader), "journal fixture is not hot");
    journal.back() ^= 0x01U;
    image.write("/security/policy-transaction.journal", journal);
    image.save(output);
    std::cout << "POLICY_JOURNAL_FIXTURE_OK mode=corrupt-journal zenovfs_checksum=valid zptj_digest=invalid bytes="
              << journal.size() << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: zenovfs-policy-journal-fixture <--hot-zmid|--corrupt-journal> <input.img> <output.img>\n";
            return 2;
        }
        const std::string mode = argv[1];
        if (mode == "--hot-zmid") make_hot_zmid(argv[2], argv[3]);
        else if (mode == "--corrupt-journal") make_corrupt_journal(argv[2], argv[3]);
        else throw std::runtime_error("unknown mode: " + mode);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-policy-journal-fixture: " << error.what() << '\n';
        return 1;
    }
}
