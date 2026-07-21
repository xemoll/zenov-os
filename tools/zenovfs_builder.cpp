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

namespace {
constexpr std::uint32_t kSectorSize = 512, kTotalSectors = 32768, kEntryCount = 128, kEntrySectors = 16, kDataStart = 32, kSlotSectors = 128;
constexpr std::uint32_t kMaxFileBytes = kSlotSectors * kSectorSize;
constexpr std::uint16_t kAuditCapacity = 64;
#pragma pack(push, 1)
struct Superblock { char magic[8]; std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation; char label[16]; std::uint8_t reserved[460]; };
struct Entry { std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved; };
struct AuditHeader {
    char magic[4]; std::uint16_t schema, header_size, record_size, capacity;
    std::uint32_t count, next_index, next_sequence;
    std::uint8_t anchor_hash[32], head_hash[32], reserved[8];
};
struct AuditRecord {
    std::uint32_t sequence, tick; std::uint8_t action, verdict; std::uint16_t path_length;
    char path[48]; std::uint8_t digest[32], record_hash[32], reserved[4];
};
struct AuditJournal { AuditHeader header; AuditRecord records[kAuditCapacity]; };
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize && sizeof(Entry) == 64);
static_assert(sizeof(AuditHeader) == 96 && sizeof(AuditRecord) == 128 && sizeof(AuditJournal) == 8288);

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
void set_path(Entry& entry, const std::string& path) {
    if (path.empty() || path.size() >= sizeof(entry.path) || path.front() != '/') throw std::runtime_error("invalid ZenovFS path: " + path);
    std::memset(entry.path, 0, sizeof(entry.path));
    std::memcpy(entry.path, path.data(), path.size());
}
void add_directory(std::array<Entry, kEntryCount>& entries, std::uint32_t index, const std::string& path) {
    Entry& entry = entries.at(index); entry.used = 1; entry.type = 2; set_path(entry, path);
}
void add_file(std::vector<std::uint8_t>& disk, std::array<Entry, kEntryCount>& entries, std::uint32_t index, const std::string& path, const std::vector<std::uint8_t>& data) {
    if (data.size() > kMaxFileBytes) throw std::runtime_error("file too large");
    Entry& entry = entries.at(index); entry.used = 1; entry.type = 1; set_path(entry, path);
    entry.size = static_cast<std::uint32_t>(data.size()); entry.checksum = fnv1a(data.data(), data.size());
    const std::uint32_t first_sector = kDataStart + index * kSlotSectors;
    const std::size_t offset = static_cast<std::size_t>(first_sector) * kSectorSize;
    if (offset + data.size() > disk.size()) throw std::runtime_error("ZenovFS layout exceeds disk size");
    std::copy(data.begin(), data.end(), disk.begin() + static_cast<std::ptrdiff_t>(offset));
}
std::vector<std::uint8_t> text_bytes(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }
std::vector<std::uint8_t> empty_audit_journal() {
    AuditJournal journal{};
    journal.header.magic[0] = 'Z'; journal.header.magic[1] = 'G'; journal.header.magic[2] = 'A'; journal.header.magic[3] = 'L';
    journal.header.schema = 1; journal.header.header_size = sizeof(AuditHeader); journal.header.record_size = sizeof(AuditRecord);
    journal.header.capacity = kAuditCapacity; journal.header.next_sequence = 1;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&journal);
    return std::vector<std::uint8_t>(begin, begin + sizeof(journal));
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 21) {
            std::cerr << "usage: zenovfs-builder <hello.zex> <fileio.elf> <args.elf> <console.elf> <protect.elf> <kaccess.elf> <zenovapp.zex> <zgdb-v3> <zgdb-v4> <zgdb-tampered> <zgdb-wrong-key> <zcap-v1> <zcap-v2> <zcap-tampered> <zcap-wrong-key> <zmid-v1> <zmid-v2> <zmid-tampered> <zmid-wrong-key> <output.img>\n";
            return 2;
        }
        const auto hello = read_all(argv[1]), fileio = read_all(argv[2]), args = read_all(argv[3]), console = read_all(argv[4]);
        const auto protect = read_all(argv[5]), kaccess = read_all(argv[6]), zenovapp = read_all(argv[7]);
        const auto zgdb_v3 = read_all(argv[8]), zgdb_v4 = read_all(argv[9]), zgdb_tampered = read_all(argv[10]), zgdb_wrong_key = read_all(argv[11]);
        const auto zcap_v1 = read_all(argv[12]), zcap_v2 = read_all(argv[13]), zcap_tampered = read_all(argv[14]), zcap_wrong_key = read_all(argv[15]);
        const auto zmid_v1 = read_all(argv[16]), zmid_v2 = read_all(argv[17]), zmid_tampered = read_all(argv[18]), zmid_wrong_key = read_all(argv[19]);
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(kTotalSectors) * kSectorSize, 0);
        std::array<Entry, kEntryCount> entries{};
        Superblock super{{'Z','E','N','O','V','F','S','1'}, 1u, kTotalSectors, kEntryCount, kEntrySectors, kDataStart, kSlotSectors, 1u,
                         {'Z','E','N','O','V','D','A','T','A',0}, {0}};
        add_directory(entries, 0, "/apps"); add_directory(entries, 1, "/docs");
        add_file(disk, entries, 2, "/docs/readme.txt", text_bytes(
            "ZenovFS1 persistent volume for ZenovOS 0.1.1\n"
            "Writes use a copy-on-write slot and sector-atomic metadata commit.\n"
            "ZenovGuard enforces RSA-PSS policy and a persistent hash-chained audit journal.\n"));
        add_file(disk, entries, 3, "/apps/hello.zex", hello); add_file(disk, entries, 4, "/apps/fileio.elf", fileio);
        add_file(disk, entries, 5, "/docs/release.txt", text_bytes(
            "ZenovOS 0.1.1 security build: page permissions, recoverable user faults,\n"
            "transactional storage, signed policy and persistent audit replay.\n"));
        add_directory(entries, 6, "/config");
        add_file(disk, entries, 7, "/config/system.ini", text_bytes(
            "[system]\nversion=0.1.1\n[console]\ntheme=midnight\nprompt=zenov>\n"
            "[storage]\nmount=/data\nfilesystem=ZenovFS1\ntransaction=cow\n"
            "[security]\nengine=ZenovGuard\ndatabase=ZGDB2\nroot=6f788074c018f5aa\n"
            "capabilities=ZCAP1\ncapability_root=9202c73fad96ad66\n"
            "intelligence=ZMID1\nintelligence_root=6ca6a5275544c533\naudit=ZGAL1\n"));
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
        add_file(disk, entries, 21, "/security/zenovguard.audit", empty_audit_journal());
        add_file(disk, entries, 22, "/security/syscall-capabilities.zcap", zcap_v1);
        add_file(disk, entries, 23, "/security/syscall-capabilities.version", text_bytes("1\n"));
        add_file(disk, entries, 24, "/security/updates/zcap-v1.zcap", zcap_v1);
        add_file(disk, entries, 25, "/security/updates/zcap-v2.zcap", zcap_v2);
        add_file(disk, entries, 26, "/security/updates/zcap-tampered.zcap", zcap_tampered);
        add_file(disk, entries, 27, "/security/updates/zcap-wrong-key.zcap", zcap_wrong_key);
        add_file(disk, entries, 28, "/security/zenovguard-intelligence.zmid", zmid_v1);
        add_file(disk, entries, 29, "/security/zenovguard-intelligence.version", text_bytes("1\n"));
        add_file(disk, entries, 30, "/security/updates/zmid-v1.zmid", zmid_v1);
        add_file(disk, entries, 31, "/security/updates/zmid-v2.zmid", zmid_v2);
        add_file(disk, entries, 32, "/security/updates/zmid-tampered.zmid", zmid_tampered);
        add_file(disk, entries, 33, "/security/updates/zmid-wrong-key.zmid", zmid_wrong_key);
        add_directory(entries, 34, "/samples");
        add_file(disk, entries, 35, "/samples/clean.txt", text_bytes("ZENOV_ZMID_CLEAN_VECTOR_V1\n"));
        add_file(disk, entries, 36, "/samples/ransomware-test.bin", text_bytes("prefix-ZENOV_RANSOMWARE_TEST_V1-suffix\n"));
        add_file(disk, entries, 37, "/samples/pua-test.bin", text_bytes("ZENOV_PUA_TEST_V1\n"));
        add_file(disk, entries, 38, "/samples/malware-v2.bin", text_bytes("ZENOV_MALWARE_TEST_V2\n"));
        add_directory(entries, 39, "/quarantine");
        std::memcpy(disk.data(), &super, sizeof(super));
        std::memcpy(disk.data() + kSectorSize, entries.data(), sizeof(entries));
        std::ofstream output(argv[20], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output image");
        output.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
        if (!output) throw std::runtime_error("cannot write output image");
        std::cout << "zenovfs-builder: OK version=0.1.1 entries=" << kEntryCount
                  << " apps=7 zgdb=schema2-v3+v4+2-negative zcap=schema1-v1+v2+2-negative zmid=schema1-v1+v2+2-negative rules=hash+pattern audit=ZGAL1-8288B zenov_source_app=" << zenovapp.size() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-builder: " << error.what() << "\n";
        return 1;
    }
}
