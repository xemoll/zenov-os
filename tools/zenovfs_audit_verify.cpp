#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"

namespace {
constexpr std::uint32_t kSectorSize = 512U;
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
std::uint32_t data_sector(const Superblock& block, std::uint32_t index) { return block.data_start + index * block.slot_sectors; }
bool path_equal(const Entry& entry, const char* path) {
    const std::size_t length = std::strlen(path);
    return length < sizeof(entry.path) && std::memcmp(entry.path, path, length) == 0 && entry.path[length] == 0;
}
void print_prefix(const std::uint8_t hash[32]) {
    for (std::size_t i = 0; i < 8U; ++i) std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(hash[i]);
    std::cout << std::dec;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "usage: zenovfs-audit-verify <zenov-data.img> [--require-nonempty] [--emit-tampered <image>]\n";
            return 2;
        }
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 known-answer test failed");
        bool require_nonempty = false;
        std::string tampered_output;
        for (int i = 2; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--require-nonempty") require_nonempty = true;
            else if (argument == "--emit-tampered" && i + 1 < argc) tampered_output = argv[++i];
            else throw std::runtime_error("unknown or incomplete argument: " + argument);
        }

        std::vector<std::uint8_t> image = read_image(argv[1]);
        if (std::memcmp(image.data(), "ZENOVFS1", 8U) != 0) throw std::runtime_error("not a ZenovFS1 image");
        const auto* block = reinterpret_cast<const Superblock*>(image.data());
        if (!block->entry_count || block->entry_count > 128U || !block->slot_sectors ||
            image.size() < static_cast<std::size_t>(block->total_sectors) * kSectorSize) throw std::runtime_error("invalid ZenovFS geometry");
        const auto* table = reinterpret_cast<const Entry*>(image.data() + kSectorSize);
        int found = -1;
        for (std::uint32_t i = 0; i < block->entry_count; ++i) {
            if (table[i].used && table[i].type == 1U && path_equal(table[i], kAuditPath)) {
                if (found >= 0) throw std::runtime_error("duplicate audit path");
                found = static_cast<int>(i);
            }
        }
        if (found < 0) throw std::runtime_error("audit path missing");
        const Entry& entry = table[static_cast<std::uint32_t>(found)];
        if (entry.flags || entry.size != sizeof(zenov_audit_host::Journal)) throw std::runtime_error("invalid audit metadata");
        const std::size_t payload_offset = static_cast<std::size_t>(data_sector(*block, static_cast<std::uint32_t>(found))) * kSectorSize;
        if (payload_offset + sizeof(zenov_audit_host::Journal) > image.size()) throw std::runtime_error("audit payload outside image");
        const auto* journal = reinterpret_cast<const zenov_audit_host::Journal*>(image.data() + payload_offset);
        if (zenov_audit_host::fnv1a(reinterpret_cast<const std::uint8_t*>(journal), sizeof(*journal)) != entry.checksum) throw std::runtime_error("audit filesystem checksum mismatch");
        if (!zenov_audit_host::verify(*journal, require_nonempty)) throw std::runtime_error("audit hash-chain mismatch");

        std::cout << "zenovfs-audit-verify: OK count=" << journal->header.count << " next=" << journal->header.next_sequence << " head=";
        print_prefix(journal->header.head_hash);
        std::cout << "\n";

        if (!tampered_output.empty()) {
            if (!journal->header.count) throw std::runtime_error("cannot tamper an empty audit journal");
            const std::uint32_t first = journal->header.count == zenov_audit_host::kCapacity ? journal->header.next_index : 0U;
            const std::size_t record_offset = payload_offset + sizeof(zenov_audit_host::Header) + static_cast<std::size_t>(first) * sizeof(zenov_audit_host::Record);
            image[record_offset + offsetof(zenov_audit_host::Record, path)] ^= 0x01U;
            auto* mutable_table = reinterpret_cast<Entry*>(image.data() + kSectorSize);
            mutable_table[static_cast<std::uint32_t>(found)].checksum = zenov_audit_host::fnv1a(image.data() + payload_offset, sizeof(zenov_audit_host::Journal));
            write_image(tampered_output, image);
            std::cout << "zenovfs-audit-verify: emitted FNV-repaired hash-chain tamper " << tampered_output << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-audit-verify: " << error.what() << "\n";
        return 1;
    }
}
