#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../security/zrwp_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Record {
    std::uint8_t type, operations;
    std::uint16_t path_length;
    char path[48];
    std::uint8_t digest[32];
    std::uint8_t reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(Record) == 96U);
constexpr std::uint8_t kProtected = 1U, kWriter = 2U;
constexpr std::uint8_t kWrite = 1U, kRename = 2U, kRemove = 4U;
using Bytes = std::vector<std::uint8_t>;

std::array<std::uint8_t, 32> fileio_digest() {
    return {0x5a,0xcc,0x70,0xa7,0x8b,0xd8,0x30,0xcd,0x7b,0x04,0x79,0x9b,0xf3,0xc2,0xbc,0x22,
            0x90,0x5a,0xc5,0x30,0x70,0xdb,0x00,0xb5,0x35,0x68,0x7c,0x8b,0x70,0x3a,0x93,0x4e};
}
Record record(std::uint8_t type, std::uint8_t operations, const std::string& path,
              const std::array<std::uint8_t, 32>& digest = {}) {
    if ((type != kProtected && type != kWriter) || !operations || (operations & ~(kWrite | kRename | kRemove)) ||
        path.empty() || path.size() >= 48U || path.front() != '/') {
        throw std::runtime_error("invalid ZRWP record");
    }
    Record output{};
    output.type = type;
    output.operations = operations;
    output.path_length = static_cast<std::uint16_t>(path.size());
    std::memcpy(output.path, path.data(), path.size());
    std::memcpy(output.digest, digest.data(), digest.size());
    return output;
}
std::vector<Record> records() {
    return {
        record(kProtected, kWrite, "/apps/userio.txt"),
        record(kProtected, kWrite | kRename | kRemove, "/docs"),
        record(kWriter, kWrite, "/apps/fileio.elf", fileio_digest()),
    };
}
Bytes build(const unsigned char header[96], const unsigned char signature[256], const std::vector<Record>& policy) {
    Bytes output(96U + policy.size() * sizeof(Record) + 256U);
    std::memcpy(output.data(), header, 96U);
    std::memcpy(output.data() + 96U, policy.data(), policy.size() * sizeof(Record));
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
            std::cerr << "usage: zrwp-builder <v1.zrwp> <v2.zrwp> <tampered.zrwp> <wrong-key.zrwp>\n";
            return 2;
        }
        const auto policy = records();
        auto v1 = build(kZrwpHeaderV1, kZrwpSignatureV1, policy);
        auto v2 = build(kZrwpHeaderV2, kZrwpSignatureV2, policy);
        auto tampered = v2;
        auto wrong_key = v2;
        tampered[120] ^= 0x01U;
        wrong_key[80] ^= 0xA5U;
        write_all(argv[1], v1);
        write_all(argv[2], v2);
        write_all(argv[3], tampered);
        write_all(argv[4], wrong_key);
        std::cout << "zrwp-builder: OK schema=1 records=" << policy.size()
                  << " v1=audit v2=block protected=2 writers=1 negative=2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zrwp-builder: " << error.what() << "\n";
        return 1;
    }
}
