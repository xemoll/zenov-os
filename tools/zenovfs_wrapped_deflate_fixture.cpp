#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kEntryCount = 128U;
constexpr std::uint32_t kDataStart = 32U;
constexpr std::uint32_t kSlotSectors = 128U;
constexpr std::uint32_t kMaxFileBytes = kSectorSize * kSlotSectors;

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
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize);
static_assert(sizeof(Entry) == 64U);

struct Spec { const char* source_name; const char* image_path; };
constexpr Spec kSpecs[] = {
    {"eicar-gzip.gz", "/samples/eicar-gzip.gz"},
    {"eicar-gzip-double.gz", "/samples/eicar-gzip-double.gz"},
    {"eicar-zlib.zlib", "/samples/eicar-zlib.zlib"},
    {"clean-gzip.gz", "/samples/clean-gzip.gz"},
    {"clean-zlib.zlib", "/samples/clean-zlib.zlib"},
    {"gzip-crc-corrupt.gz", "/samples/gzip-crc-corrupt.gz"},
    {"zlib-adler-corrupt.zlib", "/samples/zlib-adler-corrupt.zlib"},
};

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open: " + path.string());
    const std::streamoff length = input.tellg();
    if (length < 0) throw std::runtime_error("cannot size: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()),
                                  static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("cannot read: " + path.string());
    return bytes;
}

void write_all(const std::filesystem::path& path,
               const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create: " + path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write: " + path.string());
}

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

std::string entry_path(const Entry& entry) {
    std::size_t length = 0U;
    while (length < sizeof(entry.path) && entry.path[length]) ++length;
    if (length == sizeof(entry.path)) return {};
    return std::string(entry.path, length);
}

bool valid_image(std::vector<std::uint8_t>& disk,
                 const Superblock*& super,
                 Entry*& entries) {
    if (disk.size() < kSectorSize + kEntryCount * sizeof(Entry)) return false;
    super = reinterpret_cast<const Superblock*>(disk.data());
    static constexpr char kMagic[8] = {'Z','E','N','O','V','F','S','1'};
    if (std::memcmp(super->magic, kMagic, sizeof(kMagic)) != 0 ||
        super->version != 1U || super->entry_count != kEntryCount ||
        super->data_start != kDataStart || super->slot_sectors != kSlotSectors ||
        disk.size() != static_cast<std::size_t>(super->total_sectors) * kSectorSize) {
        return false;
    }
    entries = reinterpret_cast<Entry*>(disk.data() + kSectorSize);
    return true;
}

Entry* find_entry(Entry* entries, const std::string& path) {
    for (std::uint32_t index = 0U; index < kEntryCount; ++index) {
        if (entries[index].used && entry_path(entries[index]) == path) return &entries[index];
    }
    return nullptr;
}

std::uint32_t free_index(Entry* entries) {
    for (std::uint32_t index = 0U; index < kEntryCount; ++index) {
        if (!entries[index].used) return index;
    }
    throw std::runtime_error("ZenovFS entry table is full");
}

std::vector<std::uint8_t> entry_bytes(const std::vector<std::uint8_t>& disk,
                                      const Entry* entries,
                                      const Entry& entry) {
    const std::ptrdiff_t signed_index = &entry - entries;
    if (signed_index < 0 || signed_index >= static_cast<std::ptrdiff_t>(kEntryCount) ||
        entry.type != 1U || entry.size > kMaxFileBytes) {
        throw std::runtime_error("invalid runtime file entry");
    }
    const std::size_t index = static_cast<std::size_t>(signed_index);
    const std::size_t offset =
        static_cast<std::size_t>(kDataStart) * kSectorSize +
        index * static_cast<std::size_t>(kSlotSectors) * kSectorSize;
    if (offset > disk.size() || entry.size > disk.size() - offset) {
        throw std::runtime_error("runtime file bytes outside image");
    }
    std::vector<std::uint8_t> bytes(
        disk.begin() + static_cast<std::ptrdiff_t>(offset),
        disk.begin() + static_cast<std::ptrdiff_t>(offset + entry.size));
    if (fnv1a(bytes.data(), bytes.size()) != entry.checksum) {
        throw std::runtime_error("runtime file checksum mismatch: " + entry_path(entry));
    }
    return bytes;
}

void add_file(std::vector<std::uint8_t>& disk,
              Entry* entries,
              const std::string& path,
              const std::vector<std::uint8_t>& bytes) {
    if (path.empty() || path.front() != '/' || path.size() >= sizeof(Entry::path) ||
        bytes.size() > kMaxFileBytes || find_entry(entries, path)) {
        throw std::runtime_error("invalid fixture: " + path);
    }
    const std::uint32_t index = free_index(entries);
    const std::size_t offset =
        static_cast<std::size_t>(kDataStart + index * kSlotSectors) * kSectorSize;
    if (offset + kMaxFileBytes > disk.size()) throw std::runtime_error("fixture slot outside image");
    std::fill(disk.begin() + static_cast<std::ptrdiff_t>(offset),
              disk.begin() + static_cast<std::ptrdiff_t>(offset + kMaxFileBytes), 0U);
    std::copy(bytes.begin(), bytes.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
    Entry entry{};
    entry.used = 1U;
    entry.type = 1U;
    std::memcpy(entry.path, path.data(), path.size());
    entry.size = static_cast<std::uint32_t>(bytes.size());
    entry.checksum = fnv1a(bytes.data(), bytes.size());
    entries[index] = entry;
}

void require_file_bytes(const std::vector<std::uint8_t>& disk,
                        Entry* entries,
                        const char* path,
                        const std::vector<std::uint8_t>& expected) {
    const Entry* entry = find_entry(entries, path);
    if (!entry || entry->type != 1U) {
        throw std::runtime_error(std::string("missing runtime file: ") + path);
    }
    if (entry_bytes(disk, entries, *entry) != expected) {
        throw std::runtime_error(std::string("runtime bytes changed: ") + path);
    }
}

void require_absent(Entry* entries, const char* path) {
    if (find_entry(entries, path)) throw std::runtime_error(std::string("unexpected runtime file: ") + path);
}

bool starts_with(const std::string& text, const char* prefix) {
    const std::size_t size = std::strlen(prefix);
    return text.size() >= size && text.compare(0U, size, prefix) == 0;
}

bool ends_with(const std::string& text, const char* suffix) {
    const std::size_t size = std::strlen(suffix);
    return text.size() >= size && text.compare(text.size() - size, size, suffix) == 0;
}

struct QuarantinePair {
    const Entry* payload = nullptr;
    const Entry* metadata = nullptr;
};

QuarantinePair require_quarantine_pair(Entry* entries) {
    QuarantinePair pair{};
    for (std::uint32_t index = 0U; index < kEntryCount; ++index) {
        if (!entries[index].used || entries[index].type != 1U) continue;
        const std::string path = entry_path(entries[index]);
        if (!starts_with(path, "/quarantine/q-")) continue;
        if (ends_with(path, ".qtn.meta")) {
            if (pair.metadata) throw std::runtime_error("multiple wrapped quarantine metadata files");
            pair.metadata = &entries[index];
        } else if (ends_with(path, ".qtn")) {
            if (pair.payload) throw std::runtime_error("multiple wrapped quarantine payload files");
            pair.payload = &entries[index];
        }
    }
    if (!pair.payload || !pair.metadata ||
        entry_path(*pair.metadata) != entry_path(*pair.payload) + ".meta") {
        throw std::runtime_error("wrapped quarantine pair is incomplete or mismatched");
    }
    return pair;
}

int build_fixture(const std::filesystem::path& base,
                  const std::filesystem::path& corpus,
                  const std::filesystem::path& output) {
    auto disk = read_all(base);
    const Superblock* super = nullptr;
    Entry* entries = nullptr;
    if (!valid_image(disk, super, entries)) throw std::runtime_error("invalid ZenovFS1 base image");
    for (const auto& spec : kSpecs) add_file(disk, entries, spec.image_path,
                                             read_all(corpus / spec.source_name));
    write_all(output, disk);
    std::cout << "ZENOV_WRAPPED_DEFLATE_FIXTURE_OK files=" << std::size(kSpecs)
              << " image=" << output << "\n";
    return 0;
}

int verify_runtime(const std::filesystem::path& image,
                   const std::filesystem::path& corpus) {
    auto disk = read_all(image);
    const Superblock* super = nullptr;
    Entry* entries = nullptr;
    if (!valid_image(disk, super, entries)) throw std::runtime_error("invalid ZenovFS1 runtime image");
    require_absent(entries, "/samples/eicar-gzip.gz");
    require_absent(entries, "/wrapped-copy.zlib");

    for (const auto& spec : kSpecs) {
        if (std::string(spec.image_path) == "/samples/eicar-gzip.gz") continue;
        require_file_bytes(disk, entries, spec.image_path,
                           read_all(corpus / spec.source_name));
    }

    const QuarantinePair pair = require_quarantine_pair(entries);
    const std::vector<std::uint8_t> expected_payload = read_all(corpus / "eicar-gzip.gz");
    if (entry_bytes(disk, entries, *pair.payload) != expected_payload) {
        throw std::runtime_error("quarantine payload differs from original GZIP bytes");
    }
    static constexpr char kExpectedMetadata[] =
        "ZQMD1\n"
        "original=/samples/eicar-gzip.gz\n"
        "signature=GZip:Eicar.Test.File\n"
        "verdict=INFECTED\n";
    const std::vector<std::uint8_t> metadata = entry_bytes(disk, entries, *pair.metadata);
    if (metadata.size() != sizeof(kExpectedMetadata) - 1U ||
        !std::equal(metadata.begin(), metadata.end(),
                    reinterpret_cast<const std::uint8_t*>(kExpectedMetadata))) {
        throw std::runtime_error("quarantine metadata is not canonical");
    }

    std::cout << "ZENOV_WRAPPED_DEFLATE_RUNTIME_OK quarantine=2 blocked-copy=absent retained="
              << (std::size(kSpecs) - 1U)
              << " bytes=exact metadata=canonical\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--build") {
            return build_fixture(argv[2], argv[3], argv[4]);
        }
        if (argc == 4 && std::string(argv[1]) == "--verify") {
            return verify_runtime(argv[2], argv[3]);
        }
        std::cerr << "usage:\n"
                     "  zenovfs-wrapped-deflate-fixture --build <base.img> <corpus-dir> <output.img>\n"
                     "  zenovfs-wrapped-deflate-fixture --verify <runtime.img> <corpus-dir>\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-wrapped-deflate-fixture: " << error.what() << "\n";
        return 1;
    }
}