#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"
#include "../security/zvrt_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint16_t schema, header_size;
    std::uint32_t manifest_version, minimum_engine, record_count, payload_size;
    std::uint32_t chunk_size, flags;
    std::uint8_t payload_sha256[32], key_id[8], reserved[8];
};
struct Record {
    char path[48];
    std::uint32_t file_size, chunk_size, leaf_count, flags;
    std::uint8_t merkle_root[32], file_sha256[32];
};
#pragma pack(pop)
static_assert(sizeof(Header) == 80U && sizeof(Record) == 128U);

constexpr std::uint32_t kSchema = 1U, kEngine = 0x00000101U;
constexpr std::uint32_t kChunkSize = 4096U, kRequired = 1U, kImmutable = 2U;
constexpr std::uint32_t kKnownFlags = kRequired | kImmutable;

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0 || size > 8192) throw std::runtime_error("invalid ZVRT size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read input");
    return bytes;
}

bool canonical_path(const char* value, std::size_t capacity) {
    if (!value || !capacity || value[0] != '/') return false;
    std::size_t length = 0U;
    while (length < capacity && value[length]) ++length;
    if (length < 2U || length == capacity) return false;
    if (value[length - 1U] == '/') return false;
    for (std::size_t i = 0U; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        if (byte < 0x20U || byte > 0x7eU || value[i] == '\\') return false;
        if (i && value[i] == '/' && value[i - 1U] == '/') return false;
    }
    for (std::size_t i = length + 1U; i < capacity; ++i) if (value[i] != 0) return false;
    return std::strstr(value, "/../") == nullptr && std::strstr(value, "/./") == nullptr;
}

bool digest_nonzero(const std::uint8_t digest[32]) {
    std::uint8_t combined = 0U;
    for (std::size_t i = 0U; i < 32U; ++i) combined = static_cast<std::uint8_t>(combined | digest[i]);
    return combined != 0U;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string(argv[2]) != "--version") {
            std::cerr << "usage: zvrt-verify <manifest.zvrt> --version N\n";
            return 2;
        }
        const std::uint32_t expected_version = static_cast<std::uint32_t>(std::stoul(argv[3]));
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");
        const auto bytes = read_all(argv[1]);
        if (bytes.size() < sizeof(Header) + sizeof(Record) + 256U) throw std::runtime_error("truncated ZVRT manifest");
        const auto* header = reinterpret_cast<const Header*>(bytes.data());
        if (std::memcmp(header->magic, "ZVRT", 4U) != 0 || header->schema != kSchema ||
            header->header_size != sizeof(Header) || header->manifest_version != expected_version ||
            header->minimum_engine > kEngine || header->record_count != 4U ||
            header->payload_size != header->record_count * sizeof(Record) ||
            header->chunk_size != kChunkSize || header->flags != 1U ||
            sizeof(Header) + header->payload_size + 256U != bytes.size()) {
            throw std::runtime_error("invalid ZVRT header");
        }
        if (std::memcmp(header->key_id, kZvrtRootKeyId, sizeof(header->key_id)) != 0) {
            throw std::runtime_error("unexpected ZVRT key id");
        }
        for (const auto byte : header->reserved) if (byte != 0U) throw std::runtime_error("non-zero ZVRT header reserved bytes");
        const auto payload_hash = zenov_audit_host::sha256(bytes.data() + sizeof(Header), header->payload_size);
        if (std::memcmp(payload_hash.data(), header->payload_sha256, payload_hash.size()) != 0) {
            throw std::runtime_error("ZVRT payload digest mismatch");
        }

        const auto* records = reinterpret_cast<const Record*>(bytes.data() + sizeof(Header));
        const char* expected_paths[] = {"/docs/readme.txt", "/docs/release.txt", "/samples/clean.txt", "/apps/fileio.elf"};
        const std::uint32_t expected_sizes[] = {192U, 139U, 27U, 5548U};
        const std::uint32_t expected_leaves[] = {1U, 1U, 1U, 2U};
        std::uint32_t total_leaves = 0U, multichunk = 0U;
        for (std::uint32_t i = 0U; i < header->record_count; ++i) {
            const auto& record = records[i];
            if (!canonical_path(record.path, sizeof(record.path)) || std::strcmp(record.path, expected_paths[i]) != 0 ||
                record.file_size != expected_sizes[i] || record.chunk_size != kChunkSize ||
                record.leaf_count != expected_leaves[i] || record.leaf_count != (record.file_size + kChunkSize - 1U) / kChunkSize ||
                record.flags != kKnownFlags || !digest_nonzero(record.merkle_root) || !digest_nonzero(record.file_sha256)) {
                throw std::runtime_error("invalid ZVRT record");
            }
            for (std::uint32_t previous = 0U; previous < i; ++previous) {
                if (std::strcmp(record.path, records[previous].path) == 0) throw std::runtime_error("duplicate ZVRT path");
            }
            total_leaves += record.leaf_count;
            if (record.leaf_count > 1U) ++multichunk;
        }
        if (total_leaves != 5U || multichunk != 1U) throw std::runtime_error("unexpected ZVRT fixture shape");
        std::cout << "zvrt-verify: OK version=" << expected_version << " records=" << header->record_count
                  << " chunk=" << header->chunk_size << " leaves=" << total_leaves
                  << " multichunk=" << multichunk << " key=d28215ec62269ffc\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zvrt-verify: " << error.what() << "\n";
        return 1;
    }
}
