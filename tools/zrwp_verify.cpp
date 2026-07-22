#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"
#include "../security/zrwp_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint16_t schema, header_size;
    std::uint32_t policy_version, minimum_engine, mode, record_count, payload_size;
    std::uint32_t window_ticks, max_writes, max_renames, max_removes, max_bytes;
    std::uint8_t payload_sha256[32], key_id[8], reserved[8];
};
struct Record {
    std::uint8_t type, operations;
    std::uint16_t path_length;
    char path[48];
    std::uint8_t digest[32];
    std::uint8_t reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(Header) == 96U && sizeof(Record) == 96U);
constexpr std::uint8_t kProtected = 1U, kWriter = 2U, kKnownOperations = 1U | 2U | 4U;

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path);
    input.seekg(0, std::ios::end); const auto size = input.tellg(); input.seekg(0, std::ios::beg);
    if (size <= 0 || size > 16384) throw std::runtime_error("invalid ZRWP size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read input");
    return bytes;
}
bool zero(const std::uint8_t* data, std::size_t size) {
    std::uint8_t value = 0; for (std::size_t i = 0; i < size; ++i) value = static_cast<std::uint8_t>(value | data[i]); return value == 0;
}
bool canonical_path(const Record& record) {
    if (!record.path_length || record.path_length >= sizeof(record.path) || record.path[0] != '/' || record.path[record.path_length] != 0) return false;
    for (std::size_t i = record.path_length + 1U; i < sizeof(record.path); ++i) if (record.path[i] != 0) return false;
    return true;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string(argv[2]) != "--version") {
            std::cerr << "usage: zrwp-verify <policy.zrwp> --version N\n"; return 2;
        }
        const std::uint32_t expected = static_cast<std::uint32_t>(std::stoul(argv[3]));
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");
        const auto bytes = read_all(argv[1]);
        if (bytes.size() < sizeof(Header) + 256U) throw std::runtime_error("truncated ZRWP");
        const auto* header = reinterpret_cast<const Header*>(bytes.data());
        if (std::memcmp(header->magic, "ZRWP", 4U) || header->schema != 1U || header->header_size != sizeof(Header) ||
            header->policy_version != expected || header->minimum_engine > 0x00000101U || header->mode > 1U ||
            !header->record_count || header->record_count > 16U || header->payload_size != header->record_count * sizeof(Record) ||
            sizeof(Header) + header->payload_size + 256U != bytes.size() || !header->window_ticks || !header->max_writes ||
            !header->max_renames || !header->max_removes || !header->max_bytes || !zero(header->reserved, sizeof(header->reserved))) {
            throw std::runtime_error("invalid ZRWP header");
        }
        if (std::memcmp(header->key_id, kZrwpRootKeyId, sizeof(header->key_id))) throw std::runtime_error("unexpected ZRWP key id");
        const auto payload_hash = zenov_audit_host::sha256(bytes.data() + sizeof(Header), header->payload_size);
        if (std::memcmp(payload_hash.data(), header->payload_sha256, payload_hash.size())) throw std::runtime_error("payload digest mismatch");
        const auto* records = reinterpret_cast<const Record*>(bytes.data() + sizeof(Header));
        std::uint32_t protected_count = 0, writer_count = 0;
        for (std::uint32_t i = 0; i < header->record_count; ++i) {
            const auto& record = records[i];
            if ((record.type != kProtected && record.type != kWriter) || !record.operations || (record.operations & ~kKnownOperations) ||
                !canonical_path(record) || !zero(record.reserved, sizeof(record.reserved))) throw std::runtime_error("invalid ZRWP record");
            for (std::uint32_t previous = 0U; previous < i; ++previous) {
                if (record.type == records[previous].type && std::strcmp(record.path, records[previous].path) == 0) {
                    throw std::runtime_error("duplicate ZRWP identity");
                }
            }
            if (record.type == kProtected) { if (!zero(record.digest, sizeof(record.digest))) throw std::runtime_error("protected record digest must be zero"); ++protected_count; }
            else { if (zero(record.digest, sizeof(record.digest))) throw std::runtime_error("writer digest missing"); ++writer_count; }
        }
        if (protected_count != 2U || writer_count != 1U || header->record_count != 3U || header->mode != (expected == 1U ? 0U : 1U)) {
            throw std::runtime_error("unexpected ZRWP fixture contents");
        }
        std::cout << "zrwp-verify: OK version=" << expected << " mode=" << (header->mode ? "block" : "audit")
                  << " protected=" << protected_count << " writers=" << writer_count << " key=7186b2bd819e47dc\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zrwp-verify: " << error.what() << "\n"; return 1;
    }
}
