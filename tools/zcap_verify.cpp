#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"
#include "../security/zcap_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Header {
    char magic[4];
    std::uint16_t schema, header_size;
    std::uint32_t policy_version, minimum_engine, record_count, payload_size;
    std::uint8_t payload_sha256[32], key_id[8];
};
struct Record {
    char path[48];
    std::uint32_t mask;
    char read_scope[48];
    char write_scope[48];
    std::uint8_t reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(Header) == 64U && sizeof(Record) == 160U);

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > 65536) throw std::runtime_error("invalid ZCAP size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("cannot read input");
    return bytes;
}
bool canonical_string(const char* value, std::size_t size, bool allow_empty) {
    std::size_t length = 0;
    while (length < size && value[length]) ++length;
    if (length == size || (!allow_empty && length == 0U)) return false;
    for (std::size_t i = length + 1U; i < size; ++i) if (value[i] != 0) return false;
    return length == 0U || value[0] == '/';
}

struct ExpectedProfile {
    const char* path;
    std::uint32_t v1_mask;
    std::uint32_t v2_mask;
    const char* read_scope;
    const char* write_scope;
};
constexpr std::array<ExpectedProfile, 7> kExpectedProfiles{{
    {"/apps/hello.zex", 0x01U, 0x00U, "", ""},
    {"/apps/fileio.elf", 0x7DU, 0x7DU, "/apps/userio.txt", "/apps/userio.txt"},
    {"/apps/args.elf", 0x05U, 0x05U, "/docs/readme.txt", ""},
    {"/apps/console.elf", 0x81U, 0x81U, "", ""},
    {"/apps/protect.elf", 0x01U, 0x01U, "", ""},
    {"/apps/kaccess.elf", 0x00U, 0x00U, "", ""},
    {"/apps/zenovapp.zex", 0x01U, 0x01U, "", ""},
}};
}

int main(int argc, char** argv) {
    try {
        std::string path;
        std::uint32_t expected_version = 0, expected_hello_mask = 0xFFFFFFFFU;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--version" && i + 1 < argc) expected_version = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            else if (arg == "--hello-mask" && i + 1 < argc) expected_hello_mask = static_cast<std::uint32_t>(std::stoul(argv[++i], nullptr, 0));
            else if (path.empty()) path = arg;
            else throw std::runtime_error("unexpected argument: " + arg);
        }
        if (path.empty() || !expected_version || expected_hello_mask == 0xFFFFFFFFU) {
            std::cerr << "usage: zcap-verify <policy.zcap> --version N --hello-mask N\n";
            return 2;
        }
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");
        const auto bytes = read_all(path);
        if (bytes.size() < sizeof(Header) + 256U) throw std::runtime_error("truncated policy");
        const auto* header = reinterpret_cast<const Header*>(bytes.data());
        if (std::memcmp(header->magic, "ZCAP", 4U) != 0 || header->schema != 1U || header->header_size != sizeof(Header) ||
            header->policy_version != expected_version || header->minimum_engine > 0x00000101U || header->record_count != 7U ||
            header->payload_size != 7U * sizeof(Record) || sizeof(Header) + header->payload_size + 256U != bytes.size()) {
            throw std::runtime_error("invalid ZCAP header");
        }
        if (std::memcmp(header->key_id, kZcapRootKeyId, sizeof(header->key_id)) != 0) throw std::runtime_error("unexpected ZCAP key id");
        const auto payload_hash = zenov_audit_host::sha256(bytes.data() + sizeof(Header), header->payload_size);
        if (std::memcmp(payload_hash.data(), header->payload_sha256, payload_hash.size()) != 0) throw std::runtime_error("payload digest mismatch");
        const auto* records = reinterpret_cast<const Record*>(bytes.data() + sizeof(Header));
        std::array<bool, kExpectedProfiles.size()> seen{};
        for (std::uint32_t i = 0; i < header->record_count; ++i) {
            if (!canonical_string(records[i].path, sizeof(records[i].path), false) ||
                !canonical_string(records[i].read_scope, sizeof(records[i].read_scope), true) ||
                !canonical_string(records[i].write_scope, sizeof(records[i].write_scope), true)) throw std::runtime_error("non-canonical record string");
            for (std::uint8_t byte : records[i].reserved) if (byte != 0U) throw std::runtime_error("non-zero reserved record bytes");
            if ((records[i].mask & ~0xFFU) != 0U) throw std::runtime_error("unknown capability bit");
            const bool needs_read_scope = (records[i].mask & (0x04U | 0x10U)) != 0U;
            const bool needs_write_scope = (records[i].mask & 0x08U) != 0U;
            if (needs_read_scope != (records[i].read_scope[0] != 0) || needs_write_scope != (records[i].write_scope[0] != 0)) {
                throw std::runtime_error("capability scope mismatch");
            }
            std::size_t match = kExpectedProfiles.size();
            for (std::size_t expected = 0; expected < kExpectedProfiles.size(); ++expected) {
                if (std::strcmp(records[i].path, kExpectedProfiles[expected].path) == 0) { match = expected; break; }
            }
            if (match == kExpectedProfiles.size() || seen[match]) throw std::runtime_error("unknown or duplicate trusted path");
            seen[match] = true;
            const auto& expected = kExpectedProfiles[match];
            const std::uint32_t expected_mask = expected_version == 1U ? expected.v1_mask : expected.v2_mask;
            if (records[i].mask != expected_mask || std::strcmp(records[i].read_scope, expected.read_scope) != 0 ||
                std::strcmp(records[i].write_scope, expected.write_scope) != 0) throw std::runtime_error("unexpected profile contents");
        }
        for (bool present : seen) if (!present) throw std::runtime_error("trusted profile missing");
        if (records[0].mask != expected_hello_mask) throw std::runtime_error("unexpected hello mask");
        std::cout << "zcap-verify: OK version=" << header->policy_version << " profiles=" << header->record_count
                  << " hello_mask=0x" << std::hex << expected_hello_mask << std::dec << " key=9202c73fad96ad66\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zcap-verify: " << error.what() << "\n";
        return 1;
    }
}
