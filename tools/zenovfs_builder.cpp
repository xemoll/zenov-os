#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenpkg/sha256.hpp"
#include "../security/zenpkg_crypto_material.hpp"

namespace {
constexpr std::uint32_t kSectorSize = 512, kTotalSectors = 32768, kEntryCount = 128, kEntrySectors = 16, kDataStart = 32, kSlotSectors = 128;
constexpr std::uint32_t kMaxFileBytes = kSlotSectors * kSectorSize;
constexpr const char* kHelloHash = "2b7ba0114d5228825b30aca30e0e978f2faf9b798cf7f5494742d7a1d330956a";
constexpr const char* kPackageV1Hash = "9dc941c4848c19007e579927bc73803431f38b8e23344adff5c775a25196f490";
constexpr const char* kPackageV2Hash = "0e0b191098517e0ae9cba1f5b953fb051f2cddc74ad9205df5229876e99044f8";
constexpr const char* kCatalogV1Hash = "62e5817029d6c2598f263a257792327ed671ce80cb68feb6efbf690bb1229c14";
constexpr const char* kCatalogV2Hash = "c3471f1afcdb365a92ac93f1ca182508aa03806bce3a9e53001c2675c914f95b";
constexpr const char* kTamperedHash = "d6bbbff66a1431843deaf52fb5a24005c110014b8390e14dd2fcee4608665d67";

#pragma pack(push, 1)
struct Superblock { char magic[8]; std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation; char label[16]; std::uint8_t reserved[460]; };
struct Entry { std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved; };
struct CatalogHeader {
    char magic[4]; std::uint16_t schema, header_size; std::uint32_t catalog_version, minimum_engine, record_count, payload_size;
    std::uint8_t payload_sha256[32]; std::uint8_t key_id[8];
};
struct CatalogRecord {
    char name[32], version[16], entrypoint[48]; std::uint32_t package_size, payload_size;
    std::uint8_t payload_type, flags; std::uint16_t reserved;
    std::uint8_t package_sha256[32], payload_sha256[32];
};
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize && sizeof(Entry) == 64);
static_assert(sizeof(CatalogHeader) == 64 && sizeof(CatalogRecord) == 172);

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}

std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open seed file: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(kMaxFileBytes)) throw std::runtime_error("seed file exceeds ZenovFS slot capacity");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input && !bytes.empty()) throw std::runtime_error("cannot read seed file");
    return bytes;
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) output.push_back(static_cast<std::uint8_t>(value >> shift));
}
void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) output.push_back(static_cast<std::uint8_t>(value >> shift));
}

template <std::size_t N>
void set_fixed(char (&output)[N], const std::string& value) {
    if (value.empty() || value.size() >= N) throw std::runtime_error("fixed catalog field overflow");
    std::memset(output, 0, N); std::memcpy(output, value.data(), value.size());
}

std::vector<std::uint8_t> package_manifest(const std::string& version) {
    const std::string entry = "/data/apps/pkg-hello-native-" + version + ".zex";
    const std::string text =
        "format=zenpkg-manifest-1\n"
        "name=hello-native\n"
        "version=" + version + "\n"
        "architecture=i686\n"
        "target=i686-zenov-none\n"
        "kind=application\n"
        "entrypoint=" + entry + "\n"
        "payload_type=zex1\n"
        "runtime=native\n"
        "min_os=0.1.1\n"
        "license=MIT\n"
        "source=https://github.com/xemoll/zenov-os\n"
        "asset_policy=redistributable\n"
        "capability=abi.zex1.v1\n"
        "capability=kernel.ring3\n"
        "capability=storage.zenovfs1\n";
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> build_package(const std::string& version, const std::vector<std::uint8_t>& payload) {
    const auto manifest = package_manifest(version);
    std::vector<std::uint8_t> header;
    header.reserve(128);
    const std::uint8_t magic[8] = {'Z','E','N','P','K','G','1',0};
    header.insert(header.end(), magic, magic + 8);
    append_u32(header, 1); append_u32(header, 0); append_u64(header, manifest.size()); append_u64(header, payload.size());
    const auto manifest_digest = zenpkg::Sha256::hash(manifest);
    const auto payload_digest = zenpkg::Sha256::hash(payload);
    header.insert(header.end(), manifest_digest.begin(), manifest_digest.end());
    header.insert(header.end(), payload_digest.begin(), payload_digest.end());
    const auto header_digest = zenpkg::Sha256::hash(header);
    header.insert(header.end(), header_digest.begin(), header_digest.end());
    if (header.size() != 128) throw std::runtime_error("ZENPKG1 header construction failed");
    header.insert(header.end(), manifest.begin(), manifest.end());
    header.insert(header.end(), payload.begin(), payload.end());
    if (header.size() > kMaxFileBytes) throw std::runtime_error("generated package exceeds ZenovFS slot");
    return header;
}

CatalogRecord package_record(const std::string& version, const std::vector<std::uint8_t>& package, const std::vector<std::uint8_t>& payload) {
    CatalogRecord record{};
    set_fixed(record.name, "hello-native"); set_fixed(record.version, version);
    set_fixed(record.entrypoint, "/data/apps/pkg-hello-native-" + version + ".zex");
    record.package_size = static_cast<std::uint32_t>(package.size());
    record.payload_size = static_cast<std::uint32_t>(payload.size());
    record.payload_type = 1;
    const auto package_digest = zenpkg::Sha256::hash(package);
    const auto payload_digest = zenpkg::Sha256::hash(payload);
    std::copy(package_digest.begin(), package_digest.end(), record.package_sha256);
    std::copy(payload_digest.begin(), payload_digest.end(), record.payload_sha256);
    return record;
}

std::vector<std::uint8_t> build_catalog(std::uint32_t version, const std::vector<CatalogRecord>& records, const unsigned char signature[256]) {
    CatalogHeader header{{'Z','P','C','2'}, 2, sizeof(CatalogHeader), version, 0x00000102u,
                         static_cast<std::uint32_t>(records.size()), static_cast<std::uint32_t>(records.size() * sizeof(CatalogRecord)), {}, {}};
    std::vector<std::uint8_t> payload(records.size() * sizeof(CatalogRecord));
    if (!payload.empty()) std::memcpy(payload.data(), records.data(), payload.size());
    const auto payload_digest = zenpkg::Sha256::hash(payload);
    std::copy(payload_digest.begin(), payload_digest.end(), header.payload_sha256);
    std::copy(zenpkg_crypto::kZenpkgRootKeyId, zenpkg_crypto::kZenpkgRootKeyId + 8, header.key_id);
    std::vector<std::uint8_t> output(sizeof(header));
    std::memcpy(output.data(), &header, sizeof(header));
    output.insert(output.end(), payload.begin(), payload.end());
    output.insert(output.end(), signature, signature + 256);
    return output;
}

void require_hash(const std::vector<std::uint8_t>& bytes, const char* expected, const char* label) {
    const std::string actual = zenpkg::sha256_hex(bytes);
    if (actual != expected) throw std::runtime_error(std::string(label) + " deterministic SHA-256 mismatch: " + actual);
}

void set_path(Entry& entry, const std::string& path) {
    if (path.empty() || path.size() >= sizeof(entry.path) || path.front() != '/') throw std::runtime_error("invalid ZenovFS path: " + path);
    std::memset(entry.path, 0, sizeof(entry.path)); std::memcpy(entry.path, path.data(), path.size());
}
void add_directory(std::array<Entry, kEntryCount>& entries, std::uint32_t index, const std::string& path) {
    Entry& entry = entries.at(index); entry.used = 1; entry.type = 2; set_path(entry, path);
}
void add_file(std::vector<std::uint8_t>& disk, std::array<Entry, kEntryCount>& entries, std::uint32_t index,
              const std::string& path, const std::vector<std::uint8_t>& data) {
    if (data.size() > kMaxFileBytes) throw std::runtime_error("file too large");
    Entry& entry = entries.at(index); entry.used = 1; entry.type = 1; set_path(entry, path);
    entry.size = static_cast<std::uint32_t>(data.size()); entry.checksum = fnv1a(data.data(), data.size());
    const std::uint32_t first_sector = kDataStart + index * kSlotSectors;
    const std::size_t offset = static_cast<std::size_t>(first_sector) * kSectorSize;
    if (offset + data.size() > disk.size()) throw std::runtime_error("ZenovFS layout exceeds disk size");
    std::copy(data.begin(), data.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
}
std::vector<std::uint8_t> text_bytes(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 13) {
            std::cerr << "usage: zenovfs-builder <hello.zex> <fileio.elf> <args.elf> <console.elf> <protect.elf> <kaccess.elf> <zenovapp.zex> <zgdb-v3> <zgdb-v4> <zgdb-tampered> <zgdb-wrong-key> <output.img>\n";
            return 2;
        }
        const auto hello = read_all(argv[1]), fileio = read_all(argv[2]), args = read_all(argv[3]), console = read_all(argv[4]);
        const auto protect = read_all(argv[5]), kaccess = read_all(argv[6]), zenovapp = read_all(argv[7]);
        const auto zgdb_v3 = read_all(argv[8]), zgdb_v4 = read_all(argv[9]), zgdb_tampered = read_all(argv[10]), zgdb_wrong_key = read_all(argv[11]);
        require_hash(hello, kHelloHash, "HELLO.ZEX");
        const auto package_v1 = build_package("0.1.0", hello);
        const auto package_v2 = build_package("0.2.0", hello);
        require_hash(package_v1, kPackageV1Hash, "hello-native 0.1.0");
        require_hash(package_v2, kPackageV2Hash, "hello-native 0.2.0");
        const CatalogRecord record_v1 = package_record("0.1.0", package_v1, hello);
        const CatalogRecord record_v2 = package_record("0.2.0", package_v2, hello);
        const auto catalog_v1 = build_catalog(1, {record_v1}, zenpkg_crypto::kZenpkgCatalogV1Signature);
        const auto catalog_v2 = build_catalog(2, {record_v1, record_v2}, zenpkg_crypto::kZenpkgCatalogV2Signature);
        auto catalog_tampered = catalog_v2; catalog_tampered.at(80) ^= 0x40;
        require_hash(catalog_v1, kCatalogV1Hash, "catalog v1"); require_hash(catalog_v2, kCatalogV2Hash, "catalog v2");
        require_hash(catalog_tampered, kTamperedHash, "tampered catalog");

        std::vector<std::uint8_t> disk(static_cast<std::size_t>(kTotalSectors) * kSectorSize, 0);
        std::array<Entry, kEntryCount> entries{};
        Superblock super{{'Z','E','N','O','V','F','S','1'}, 1u, kTotalSectors, kEntryCount, kEntrySectors, kDataStart, kSlotSectors, 1u,
                         {'Z','E','N','O','V','D','A','T','A',0}, {0}};
        add_directory(entries, 0, "/apps"); add_directory(entries, 1, "/docs");
        add_file(disk, entries, 2, "/docs/readme.txt", text_bytes(
            "ZenovFS1 persistent volume for ZenovOS 0.1.1\n"
            "Writes use a copy-on-write slot and sector-atomic metadata commit.\n"
            "ZenovGuard and RSA-PSS signed ZGDB2/ZPC2 metadata enforce executable trust.\n"));
        add_file(disk, entries, 3, "/apps/hello.zex", hello); add_file(disk, entries, 4, "/apps/fileio.elf", fileio);
        add_file(disk, entries, 5, "/docs/release.txt", text_bytes(
            "ZenovOS 0.1.1 security build: page permissions, recoverable user faults,\n"
            "transactional storage, ZenovGuard, rotated ZGDB2 and signed native packages.\n"));
        add_directory(entries, 6, "/config");
        add_file(disk, entries, 7, "/config/system.ini", text_bytes(
            "[system]\nversion=0.1.1\n[console]\ntheme=midnight\nprompt=zenov>\n"
            "[storage]\nmount=/data\nfilesystem=ZenovFS1\ntransaction=cow\n"
            "[security]\nengine=ZenovGuard\ndatabase=ZGDB2\nroot=6f788074c018f5aa\npackages=ZPC2\npackage_root=30a8d71f32182651\n"));
        add_file(disk, entries, 8, "/apps/args.elf", args); add_file(disk, entries, 9, "/apps/console.elf", console);
        add_file(disk, entries, 10, "/apps/protect.elf", protect); add_file(disk, entries, 11, "/apps/kaccess.elf", kaccess);
        add_file(disk, entries, 12, "/apps/zenovapp.zex", zenovapp);
        add_directory(entries, 13, "/security"); add_directory(entries, 14, "/security/updates");
        add_file(disk, entries, 15, "/security/zenovguard.zgdb", zgdb_v3);
        add_file(disk, entries, 16, "/security/zenovguard.version", text_bytes("3\n"));
        add_file(disk, entries, 17, "/security/updates/zenovguard-v3.zgdb", zgdb_v3);
        add_file(disk, entries, 18, "/security/updates/zenovguard-v4.zgdb", zgdb_v4);
        add_file(disk, entries, 19, "/security/updates/zenovguard-tampered.zgdb", zgdb_tampered);
        add_file(disk, entries, 20, "/security/updates/zenovguard-wrong-key.zgdb", zgdb_wrong_key);
        add_directory(entries, 21, "/packages");
        add_file(disk, entries, 22, "/packages/hello-native-0.1.0.zpk", package_v1);
        add_file(disk, entries, 23, "/packages/hello-native-0.2.0.zpk", package_v2);
        add_file(disk, entries, 24, "/security/zenpkg.zpc", catalog_v1);
        add_file(disk, entries, 25, "/security/zenpkg.version", text_bytes("1\n"));
        add_file(disk, entries, 26, "/security/updates/zenpkg-v1.zpc", catalog_v1);
        add_file(disk, entries, 27, "/security/updates/zenpkg-v2.zpc", catalog_v2);
        add_file(disk, entries, 28, "/security/updates/zenpkg-tampered.zpc", catalog_tampered);
        std::memcpy(disk.data(), &super, sizeof(super)); std::memcpy(disk.data() + kSectorSize, entries.data(), sizeof(entries));
        std::ofstream output(argv[12], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output image");
        output.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
        if (!output) throw std::runtime_error("cannot write output image");
        std::cout << "zenovfs-builder: OK version=0.1.1 sectors=" << kTotalSectors << " entries=" << kEntryCount
                  << " apps=7 zgdb=schema2-v3+v4+2-negative zenpkg=zpc2-v1+v2+tampered source_app=" << zenovapp.size() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-builder: " << error.what() << "\n"; return 1;
    }
}
