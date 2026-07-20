#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kSectorSize = 512U;
constexpr std::uint32_t kExpectedSectors = 32768U;
constexpr std::uint32_t kMaxEntries = 128U;

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

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
}
std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file: " + path.string());
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("cannot determine file size: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("short read: " + path.string());
    return bytes;
}
void write_all(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const auto temporary = path.string() + ".seed.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open temporary image: " + temporary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) throw std::runtime_error("cannot write temporary image: " + temporary);
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec) throw std::runtime_error("cannot replace image: " + ec.message());
    }
}
std::string path_of(const Entry& entry) {
    const auto end = std::find(std::begin(entry.path), std::end(entry.path), '\0');
    if (end == std::end(entry.path)) throw std::runtime_error("unterminated ZenovFS path");
    return std::string(entry.path, end);
}
void validate_image(const std::vector<std::uint8_t>& image, const Superblock& super) {
    const char magic[8] = {'Z','E','N','O','V','F','S','1'};
    if (image.size() != static_cast<std::size_t>(kExpectedSectors) * kSectorSize ||
        std::memcmp(super.magic, magic, sizeof(magic)) != 0 || super.version != 1U ||
        super.total_sectors != kExpectedSectors || !super.entry_count || super.entry_count > kMaxEntries ||
        super.entry_sectors * kSectorSize < super.entry_count * sizeof(Entry) ||
        !super.slot_sectors || super.data_start < 1U + super.entry_sectors) {
        throw std::runtime_error("invalid ZenovFS1 image");
    }
    const std::uint64_t final_sector = static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(super.entry_count) * super.slot_sectors;
    if (final_sector > super.total_sectors) throw std::runtime_error("ZenovFS slots exceed image");
}
bool safe_filename(const std::string& name, const std::string& suffix) {
    if (name.size() <= suffix.size() || name.size() > 42U || name.substr(name.size() - suffix.size()) != suffix) return false;
    for (const unsigned char value : name) {
        if (!(std::islower(value) || std::isdigit(value) || value == '.' || value == '-' || value == '_')) return false;
    }
    return name.find("..") == std::string::npos;
}
void set_path(Entry& entry, const std::string& path) {
    if (path.size() < 2U || path.size() >= sizeof(entry.path) || path.front() != '/' ||
        path.back() == '/' || path.find("//") != std::string::npos) throw std::runtime_error("unsafe ZenovFS path: " + path);
    std::memset(entry.path, 0, sizeof(entry.path));
    std::memcpy(entry.path, path.data(), path.size());
}
int find_entry(const Entry* entries, std::uint32_t count, const std::string& path) {
    for (std::uint32_t i = 0; i < count; ++i) if (entries[i].used && path_of(entries[i]) == path) return static_cast<int>(i);
    return -1;
}
std::uint32_t allocate_entry(Entry* entries, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) if (!entries[i].used) return i;
    throw std::runtime_error("ZenovFS metadata table is full");
}
void add_directory(Entry* entries, std::uint32_t count, const std::string& path) {
    const int existing = find_entry(entries, count, path);
    if (existing >= 0) {
        if (entries[existing].type != 2U) throw std::runtime_error("path exists and is not a directory: " + path);
        return;
    }
    Entry& entry = entries[allocate_entry(entries, count)];
    std::memset(&entry, 0, sizeof(entry));
    entry.used = 1U; entry.type = 2U; set_path(entry, path);
}
void validate_seed_magic(const std::string& destination, const std::vector<std::uint8_t>& data) {
    if (destination.rfind("/packages/", 0) == 0) {
        const std::uint8_t magic[8] = {'Z','E','N','P','K','G','1',0};
        if (data.size() < 128U || !std::equal(std::begin(magic), std::end(magic), data.begin()))
            throw std::runtime_error("seed file is not a ZENPKG1 package: " + destination);
        return;
    }
    const std::uint8_t magic[4] = {'Z','R','M','1'};
    if (data.size() < 328U || !std::equal(std::begin(magic), std::end(magic), data.begin()))
        throw std::runtime_error("seed file is not ZRM1 metadata: " + destination);
}
void add_file(std::vector<std::uint8_t>& image, const Superblock& super, Entry* entries,
              const std::string& path, const std::vector<std::uint8_t>& data) {
    if (find_entry(entries, super.entry_count, path) >= 0) throw std::runtime_error("duplicate seed path: " + path);
    const std::uint64_t capacity = static_cast<std::uint64_t>(super.slot_sectors) * kSectorSize;
    if (data.empty() || data.size() > capacity) throw std::runtime_error("seed file does not fit ZenovFS slot: " + path);
    validate_seed_magic(path, data);
    const std::uint32_t index = allocate_entry(entries, super.entry_count);
    Entry& entry = entries[index];
    std::memset(&entry, 0, sizeof(entry));
    entry.used = 1U; entry.type = 1U; set_path(entry, path);
    entry.size = static_cast<std::uint32_t>(data.size());
    entry.checksum = fnv1a(data.data(), data.size());
    const std::uint64_t offset = (static_cast<std::uint64_t>(super.data_start) +
        static_cast<std::uint64_t>(index) * super.slot_sectors) * kSectorSize;
    std::fill(image.begin() + static_cast<std::ptrdiff_t>(offset),
        image.begin() + static_cast<std::ptrdiff_t>(offset + capacity), 0);
    std::copy(data.begin(), data.end(), image.begin() + static_cast<std::ptrdiff_t>(offset));
}
bool self_test() {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(kExpectedSectors) * kSectorSize, 0);
    auto* super = reinterpret_cast<Superblock*>(image.data());
    const Superblock value{{'Z','E','N','O','V','F','S','1'}, 1U, kExpectedSectors, kMaxEntries, 16U, 32U, 128U, 1U,
                           {'Z','E','N','O','V','D','A','T','A',0}, {0}};
    *super = value;
    validate_image(image, *super);
    auto* entries = reinterpret_cast<Entry*>(image.data() + kSectorSize);
    add_directory(entries, super->entry_count, "/packages");
    add_directory(entries, super->entry_count, "/repo");
    std::vector<std::uint8_t> package(128U, 0), metadata(328U, 0);
    const std::uint8_t package_magic[8] = {'Z','E','N','P','K','G','1',0};
    const std::uint8_t metadata_magic[4] = {'Z','R','M','1'};
    std::copy(std::begin(package_magic), std::end(package_magic), package.begin());
    std::copy(std::begin(metadata_magic), std::end(metadata_magic), metadata.begin());
    add_file(image, *super, entries, "/packages/test-1.0.0.zpk", package);
    add_file(image, *super, entries, "/repo/timestamp.zrm", metadata);
    return find_entry(entries, super->entry_count, "/packages/test-1.0.0.zpk") >= 0 &&
        find_entry(entries, super->entry_count, "/repo/timestamp.zrm") >= 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            if (!self_test()) throw std::runtime_error("self-test failed");
            std::cout << "zenovfs-package-seed: SELF_TEST_OK\n";
            return 0;
        }
        if (argc < 3) {
            std::cerr << "usage: zenovfs-package-seed <zenov-data.img> <package.zpk|metadata.zrm> [...]\n";
            return 2;
        }
        const std::filesystem::path image_path = argv[1];
        auto image = read_all(image_path);
        if (image.size() < sizeof(Superblock) + kSectorSize) throw std::runtime_error("image is truncated");
        auto* super = reinterpret_cast<Superblock*>(image.data());
        validate_image(image, *super);
        auto* entries = reinterpret_cast<Entry*>(image.data() + kSectorSize);
        add_directory(entries, super->entry_count, "/packages");
        add_directory(entries, super->entry_count, "/repo");
        add_directory(entries, super->entry_count, "/var");
        add_directory(entries, super->entry_count, "/var/lib");
        add_directory(entries, super->entry_count, "/var/lib/zenpkg");

        std::set<std::string> destinations;
        std::uint32_t packages = 0, metadata = 0;
        for (int i = 2; i < argc; ++i) {
            const std::filesystem::path source = argv[i];
            const std::string filename = source.filename().string();
            std::string destination;
            if (safe_filename(filename, ".zpk")) { destination = "/packages/" + filename; ++packages; }
            else if (safe_filename(filename, ".zrm")) { destination = "/repo/" + filename; ++metadata; }
            else throw std::runtime_error("unsafe package or metadata filename: " + filename);
            if (!destinations.insert(destination).second) throw std::runtime_error("duplicate seed destination: " + destination);
            add_file(image, *super, entries, destination, read_all(source));
        }
        write_all(image_path, image);
        std::cout << "zenovfs-package-seed: OK packages=" << packages << " metadata=" << metadata
                  << " image=" << image_path << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-package-seed: " << error.what() << "\n";
        return 1;
    }
}
