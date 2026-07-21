#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../security/zmid_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Record {
    std::uint8_t type, action;
    std::uint16_t value_length;
    std::uint8_t value[32];
    char name[48];
    std::uint8_t reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(Record) == 96U);

constexpr std::uint8_t kHash = 1U, kPattern = 2U;
constexpr std::uint8_t kBlock = 1U, kQuarantine = 2U, kAudit = 4U;
using Bytes = std::vector<std::uint8_t>;

std::array<std::uint8_t, 32> digest(std::initializer_list<std::uint8_t> bytes) {
    if (bytes.size() != 32U) throw std::runtime_error("digest must contain 32 bytes");
    std::array<std::uint8_t, 32> output{};
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return output;
}
Record hash_record(std::uint8_t action, const std::string& name, const std::array<std::uint8_t, 32>& value) {
    if (name.empty() || name.size() >= 48U) throw std::runtime_error("invalid intelligence record name");
    Record output{};
    output.type = kHash;
    output.action = action;
    output.value_length = 32U;
    std::memcpy(output.value, value.data(), value.size());
    std::memcpy(output.name, name.data(), name.size());
    return output;
}
Record pattern_record(std::uint8_t action, const std::string& name, const std::string& pattern) {
    if (name.empty() || name.size() >= 48U || pattern.empty() || pattern.size() > 32U) {
        throw std::runtime_error("invalid bounded pattern record");
    }
    Record output{};
    output.type = kPattern;
    output.action = action;
    output.value_length = static_cast<std::uint16_t>(pattern.size());
    std::memcpy(output.value, pattern.data(), pattern.size());
    std::memcpy(output.name, name.data(), name.size());
    return output;
}
std::vector<Record> version1_records() {
    return {
        hash_record(kBlock | kQuarantine, "Eicar.Test.File", digest({
            0x27,0x5a,0x02,0x1b,0xbf,0xb6,0x48,0x9e,0x54,0xd4,0x71,0x89,0x9f,0x7d,0xb9,0xd1,
            0x66,0x3f,0xc6,0x95,0xec,0x2f,0xe2,0xa2,0xc4,0x53,0x8a,0xab,0xf6,0x51,0xfd,0x0f})),
        hash_record(kBlock | kQuarantine, "Zenov.Guard.Test-Signature", digest({
            0x3e,0x98,0xe2,0xf4,0x8d,0x88,0x47,0x40,0x92,0xd6,0xf8,0x0e,0xb5,0xf9,0x04,0x1c,
            0x4a,0x77,0xc0,0xd9,0x20,0xd1,0x22,0x9e,0x6b,0xd1,0x08,0x88,0x88,0xb6,0x7c,0x82})),
        pattern_record(kBlock | kQuarantine, "Eicar.Pattern", "EICAR-STANDARD-ANTIVIRUS"),
        pattern_record(kBlock | kQuarantine, "Pattern.Ransomware.Test", "ZENOV_RANSOMWARE_TEST_V1"),
        pattern_record(kAudit, "PUA.Zenov.Test", "ZENOV_PUA_TEST_V1"),
    };
}
Bytes build(const unsigned char header[64], const unsigned char signature[256], const std::vector<Record>& records) {
    Bytes output(64U + records.size() * sizeof(Record) + 256U);
    std::memcpy(output.data(), header, 64U);
    std::memcpy(output.data() + 64U, records.data(), records.size() * sizeof(Record));
    std::memcpy(output.data() + output.size() - 256U, signature, 256U);
    return output;
}
void write_all(const std::string& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output: " + path);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write output: " + path);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            std::cerr << "usage: zmid-builder <v1.zmid> <v2.zmid> <tampered.zmid> <wrong-key.zmid>\n";
            return 2;
        }
        const auto v1_records = version1_records();
        auto v2_records = v1_records;
        v2_records.push_back(pattern_record(kBlock | kQuarantine, "Malware.Zenov.V2", "ZENOV_MALWARE_TEST_V2"));
        auto v1 = build(kZmidHeaderV1, kZmidSignatureV1, v1_records);
        auto v2 = build(kZmidHeaderV2, kZmidSignatureV2, v2_records);
        auto tampered = v2;
        auto wrong_key = v2;
        tampered[80] ^= 0x01U;
        wrong_key[56] ^= 0xA5U;
        write_all(argv[1], v1);
        write_all(argv[2], v2);
        write_all(argv[3], tampered);
        write_all(argv[4], wrong_key);
        std::cout << "zmid-builder: OK schema=1 v1_records=" << v1_records.size()
                  << " v2_records=" << v2_records.size() << " negative=2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zmid-builder: " << error.what() << "\n";
        return 1;
    }
}
