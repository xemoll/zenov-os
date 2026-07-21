#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"

namespace {
constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kPartialIndex = 96U;
constexpr std::uint32_t kJournalIndex = 112U;
constexpr std::uint8_t kFile = 1U;
constexpr std::uint32_t kMaximumPackageBytes = 64U * 1024U;
constexpr std::uint32_t kProviderOffline = 1U;
constexpr std::uint32_t kPhaseDownloading = 1U;
constexpr std::uint32_t kPhaseReady = 2U;

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

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open: " + path.string());
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("cannot determine size: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("short read: " + path.string());
    return bytes;
}

void write_all(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create: " + path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("short write: " + path.string());
}

Superblock& super(std::vector<std::uint8_t>& image) {
    return *reinterpret_cast<Superblock*>(image.data());
}
const Superblock& super(const std::vector<std::uint8_t>& image) {
    return *reinterpret_cast<const Superblock*>(image.data());
}
Entry* entries(std::vector<std::uint8_t>& image) {
    return reinterpret_cast<Entry*>(image.data() + kSectorSize);
}
const Entry* entries(const std::vector<std::uint8_t>& image) {
    return reinterpret_cast<const Entry*>(image.data() + kSectorSize);
}
std::uint32_t entry_sector(std::uint32_t index) { return 1U + index / 8U; }
std::uint32_t data_sector(const Superblock& block, std::uint32_t index) {
    return block.data_start + index * block.slot_sectors;
}

void validate_image(const std::vector<std::uint8_t>& image) {
    if (image.size() < kSectorSize || std::memcmp(image.data(), "ZENOVFS1", 8U) != 0)
        throw std::runtime_error("not a ZenovFS1 image");
    const Superblock& block = super(image);
    if (block.version != 1U || block.entry_count <= kJournalIndex || !block.slot_sectors ||
        image.size() < static_cast<std::size_t>(block.total_sectors) * kSectorSize)
        throw std::runtime_error("invalid ZenovFS1 geometry");
    const std::uint64_t final_sector = static_cast<std::uint64_t>(block.data_start) +
        static_cast<std::uint64_t>(block.entry_count) * block.slot_sectors;
    if (final_sector > block.total_sectors) throw std::runtime_error("ZenovFS slots exceed image");
}

bool path_equal(const Entry& entry, const std::string& path) {
    return path.size() < sizeof(entry.path) && std::memcmp(entry.path, path.data(), path.size()) == 0 &&
        entry.path[path.size()] == 0;
}

void require_path_absent(const std::vector<std::uint8_t>& image, const std::string& path) {
    const Entry* table = entries(image);
    for (std::uint32_t i = 0; i < super(image).entry_count; ++i) {
        if (table[i].used && path_equal(table[i], path))
            throw std::runtime_error("path already exists in seed image: " + path);
    }
}

void set_path(Entry& entry, const std::string& path) {
    if (path.empty() || path.size() >= sizeof(entry.path) || path.front() != '/')
        throw std::runtime_error("invalid ZenovFS path: " + path);
    std::memset(entry.path, 0, sizeof(entry.path));
    std::memcpy(entry.path, path.data(), path.size());
}

void put_file(std::vector<std::uint8_t>& image, std::uint32_t index, const std::string& path,
              const std::uint8_t* data, std::size_t size) {
    Superblock& block = super(image);
    Entry* table = entries(image);
    if (index >= block.entry_count || table[index].used) throw std::runtime_error("reserved seed slot is not free");
    if (size > static_cast<std::size_t>(block.slot_sectors) * kSectorSize)
        throw std::runtime_error("seed object exceeds ZenovFS slot");
    require_path_absent(image, path);
    const std::size_t offset = static_cast<std::size_t>(data_sector(block, index)) * kSectorSize;
    const std::size_t capacity = static_cast<std::size_t>(block.slot_sectors) * kSectorSize;
    if (offset + capacity > image.size()) throw std::runtime_error("seed object outside image");
    std::fill(image.begin() + static_cast<std::ptrdiff_t>(offset),
              image.begin() + static_cast<std::ptrdiff_t>(offset + capacity), 0U);
    if (size) std::copy_n(data, size, image.begin() + static_cast<std::ptrdiff_t>(offset));
    Entry entry{};
    entry.used = 1U;
    entry.type = kFile;
    entry.size = static_cast<std::uint32_t>(size);
    entry.checksum = fnv1a(data, size);
    set_path(entry, path);
    table[index] = entry;
}

std::string hex_prefix(const std::uint8_t digest[32]) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(24U);
    for (std::size_t i = 0; i < 12U; ++i) {
        output.push_back(digits[digest[i] >> 4U]);
        output.push_back(digits[digest[i] & 0x0FU]);
    }
    return output;
}

void copy_text(char* output, std::size_t capacity, const std::string& value) {
    if (value.empty() || value.size() >= capacity) throw std::runtime_error("journal text field overflow");
    std::memset(output, 0, capacity);
    std::memcpy(output, value.data(), value.size());
}

TransportJournal make_journal(const std::vector<std::uint8_t>& package, std::uint32_t phase,
                              std::uint32_t offset, const std::string& partial_path) {
    TransportJournal journal{};
    const char magic[8] = {'Z','P','T','R','N','1',0,0};
    std::memcpy(journal.magic, magic, sizeof(magic));
    journal.schema = 1U;
    journal.generation = 7U;
    journal.phase = phase;
    journal.offset = offset;
    journal.expected_length = static_cast<std::uint32_t>(package.size());
    journal.provider = kProviderOffline;
    copy_text(journal.name, sizeof(journal.name), "hello-native");
    copy_text(journal.version, sizeof(journal.version), "0.2.0");
    copy_text(journal.source_path, sizeof(journal.source_path), "/data/packages/hello-native-0.2.0.zpk");
    copy_text(journal.partial_path, sizeof(journal.partial_path), partial_path);
    const auto package_digest = zenov_audit_host::sha256(package.data(), package.size());
    const auto prefix_digest = zenov_audit_host::sha256(package.data(), offset);
    std::memcpy(journal.package_digest, package_digest.data(), package_digest.size());
    std::memcpy(journal.prefix_digest, prefix_digest.data(), prefix_digest.size());
    journal.checksum = 0U;
    journal.checksum = fnv1a(reinterpret_cast<const std::uint8_t*>(&journal), sizeof(journal));
    return journal;
}

std::vector<std::uint8_t> make_state(const std::vector<std::uint8_t>& base,
                                     const std::vector<std::uint8_t>& package,
                                     bool committed, bool ready) {
    auto image = base;
    const auto digest = zenov_audit_host::sha256(package.data(), package.size());
    const std::string key = hex_prefix(digest.data());
    const std::string partial_path = "/var/cache/zp/" + key + ".part";
    const std::string final_path = "/var/cache/zp/" + key + ".zpk";
    const std::uint32_t offset = ready ? static_cast<std::uint32_t>(package.size()) : kSectorSize;
    if (offset > package.size()) throw std::runtime_error("package is too small for a resume fixture");
    put_file(image, kPartialIndex, committed ? final_path : partial_path, package.data(), offset);
    const TransportJournal journal = make_journal(package, ready ? kPhaseReady : kPhaseDownloading,
                                                   offset, partial_path);
    put_file(image, kJournalIndex, "/var/lib/zenpkg/transport.v1",
             reinterpret_cast<const std::uint8_t*>(&journal), sizeof(journal));
    return image;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: zenpkg-blkdebug-seed <zenov-data.img> <hello-native-0.2.0.zpk> <output-dir>\n";
            return 2;
        }
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");
        const std::vector<std::uint8_t> base = read_all(argv[1]);
        const std::vector<std::uint8_t> package = read_all(argv[2]);
        validate_image(base);
        if (package.size() <= kSectorSize || package.size() > kMaximumPackageBytes ||
            package.size() > static_cast<std::size_t>(super(base).slot_sectors) * kSectorSize ||
            package.size() < 8U || std::memcmp(package.data(), "ZENPKG1", 7U) != 0)
            throw std::runtime_error("invalid package fixture");
        if (entries(base)[kPartialIndex].used || entries(base)[kJournalIndex].used)
            throw std::runtime_error("deterministic high seed slots are occupied");

        const std::filesystem::path output = argv[3];
        std::filesystem::create_directories(output);
        write_all(output / "resume.img", make_state(base, package, false, false));
        write_all(output / "ready.img", make_state(base, package, false, true));
        write_all(output / "committed.img", make_state(base, package, true, true));
        std::ofstream sectors(output / "sectors.env", std::ios::trunc);
        if (!sectors) throw std::runtime_error("cannot create sectors.env");
        sectors << "PARTIAL_ENTRY_INDEX=" << kPartialIndex << '\n'
                << "PARTIAL_ENTRY_SECTOR=" << entry_sector(kPartialIndex) << '\n'
                << "JOURNAL_ENTRY_INDEX=" << kJournalIndex << '\n'
                << "JOURNAL_ENTRY_SECTOR=" << entry_sector(kJournalIndex) << '\n';
        if (!sectors) throw std::runtime_error("cannot write sectors.env");
        std::cout << "ZENPKG_BLKDEBUG_SEED_OK partial-sector=" << entry_sector(kPartialIndex)
                  << " journal-sector=" << entry_sector(kJournalIndex)
                  << " package-bytes=" << package.size() << " output=" << output.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenpkg-blkdebug-seed: " << error.what() << '\n';
        return 1;
    }
}
