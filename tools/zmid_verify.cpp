#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"
#include "../security/zmid_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint16_t schema, header_size;
    std::uint32_t database_version, minimum_engine, record_count, payload_size;
    std::uint8_t payload_sha256[32], key_id[8];
};
struct Record {
    std::uint8_t type, action;
    std::uint16_t value_length;
    std::uint8_t value[32];
    char name[48];
    std::uint8_t reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(Header) == 64U && sizeof(Record) == 96U);
constexpr std::uint8_t kHash = 1U, kPattern = 2U;
constexpr std::uint8_t kKnownActions = 1U | 2U | 4U;

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0 || size > 16384) throw std::runtime_error("invalid ZMID size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read input");
    return bytes;
}
bool canonical_name(const char* value, std::size_t size) {
    std::size_t length = 0;
    while (length < size && value[length]) ++length;
    if (!length || length == size) return false;
    for (std::size_t i = length + 1U; i < size; ++i) if (value[i] != 0) return false;
    return true;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string(argv[2]) != "--version") {
            std::cerr << "usage: zmid-verify <database.zmid> --version N\n";
            return 2;
        }
        const std::uint32_t expected_version = static_cast<std::uint32_t>(std::stoul(argv[3]));
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");
        const auto bytes = read_all(argv[1]);
        if (bytes.size() < sizeof(Header) + 256U) throw std::runtime_error("truncated intelligence database");
        const auto* header = reinterpret_cast<const Header*>(bytes.data());
        if (std::memcmp(header->magic, "ZMID", 4U) != 0 || header->schema != 1U || header->header_size != sizeof(Header) ||
            header->database_version != expected_version || header->minimum_engine > 0x00000101U ||
            !header->record_count || header->record_count > 32U || header->payload_size != header->record_count * sizeof(Record) ||
            sizeof(Header) + header->payload_size + 256U != bytes.size()) throw std::runtime_error("invalid ZMID header");
        if (std::memcmp(header->key_id, kZmidRootKeyId, sizeof(header->key_id)) != 0) throw std::runtime_error("unexpected ZMID key id");
        const auto payload_hash = zenov_audit_host::sha256(bytes.data() + sizeof(Header), header->payload_size);
        if (std::memcmp(payload_hash.data(), header->payload_sha256, payload_hash.size()) != 0) throw std::runtime_error("payload digest mismatch");
        const auto* records = reinterpret_cast<const Record*>(bytes.data() + sizeof(Header));
        std::uint32_t hashes = 0, patterns = 0, audit_only = 0;
        for (std::uint32_t i = 0; i < header->record_count; ++i) {
            const auto& record = records[i];
            if ((record.type != kHash && record.type != kPattern) || !record.action || (record.action & ~kKnownActions) ||
                !canonical_name(record.name, sizeof(record.name))) throw std::runtime_error("invalid ZMID record metadata");
            if ((record.type == kHash && record.value_length != 32U) ||
                (record.type == kPattern && (!record.value_length || record.value_length > sizeof(record.value)))) {
                throw std::runtime_error("invalid ZMID record value length");
            }
            for (std::size_t n = record.value_length; n < sizeof(record.value); ++n) if (record.value[n] != 0U) throw std::runtime_error("non-zero ZMID value padding");
            for (std::uint8_t byte : record.reserved) if (byte != 0U) throw std::runtime_error("non-zero ZMID reserved bytes");
            if (record.type == kHash) ++hashes; else ++patterns;
            if ((record.action & 4U) && !(record.action & 1U)) ++audit_only;
        }
        const std::uint32_t expected_records = expected_version == 1U ? 5U : 6U;
        if (header->record_count != expected_records || hashes != 2U || patterns != expected_records - 2U || audit_only != 1U) {
            throw std::runtime_error("unexpected ZMID fixture contents");
        }
        std::cout << "zmid-verify: OK version=" << expected_version << " records=" << header->record_count
                  << " hashes=" << hashes << " patterns=" << patterns << " key=6ca6a5275544c533\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zmid-verify: " << error.what() << "\n";
        return 1;
    }
}
