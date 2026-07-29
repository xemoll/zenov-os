#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

using Digest = std::array<std::uint8_t, 32>;
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t kChunkSize = 4096U;
constexpr std::uint32_t kRequired = 1U, kImmutable = 2U;

Digest digest(std::initializer_list<std::uint8_t> bytes) {
    if (bytes.size() != 32U) throw std::runtime_error("digest must contain 32 bytes");
    Digest output{};
    std::size_t index = 0U;
    for (const auto byte : bytes) output[index++] = byte;
    return output;
}

Record make_record(const std::string& path, std::uint32_t size, std::uint32_t leaves,
                   const Digest& root, const Digest& file_hash) {
    if (path.empty() || path.size() >= 48U || path.front() != '/' || !size || !leaves || leaves > 16U) {
        throw std::runtime_error("invalid ZVRT record");
    }
    Record output{};
    std::memcpy(output.path, path.data(), path.size());
    output.file_size = size;
    output.chunk_size = kChunkSize;
    output.leaf_count = leaves;
    output.flags = kRequired | kImmutable;
    std::memcpy(output.merkle_root, root.data(), root.size());
    std::memcpy(output.file_sha256, file_hash.data(), file_hash.size());
    return output;
}

std::vector<Record> records() {
    return {
        make_record("/docs/readme.txt", 192U, 1U,
            digest({0xb5,0x29,0xa3,0x61,0xf4,0xf3,0x5b,0x5d,0xc3,0xb7,0x7f,0x5c,0x74,0xde,0x5d,0x14,
                    0x60,0x8a,0xc8,0xc4,0x90,0x58,0x0c,0x20,0x79,0x51,0x90,0xe4,0x38,0x97,0xe1,0xae}),
            digest({0xd3,0x84,0x49,0x40,0x6f,0x81,0xe4,0x5e,0x2e,0x8f,0xa0,0x0c,0xee,0x3a,0x87,0x61,
                    0x23,0x45,0x34,0x73,0xf5,0x60,0xaf,0x4f,0x1d,0xb9,0xc8,0xb3,0x2a,0xd0,0x33,0xdf})),
        make_record("/docs/release.txt", 139U, 1U,
            digest({0xb2,0x86,0x32,0xda,0x7c,0x72,0xcf,0xbf,0x51,0xcc,0xd3,0xba,0xc0,0xf9,0x85,0x41,
                    0x8b,0x95,0x97,0xc0,0x2f,0xd2,0x44,0x97,0xbd,0x8d,0x44,0x93,0xc1,0x15,0xf0,0x7a}),
            digest({0x72,0x67,0x2e,0xcb,0x12,0xb0,0x92,0xfe,0x69,0x36,0xff,0x8d,0x8c,0x71,0x04,0x0c,
                    0x51,0xc7,0x1c,0xb5,0xef,0xde,0x57,0xba,0x2c,0x73,0x40,0x27,0xf2,0xf8,0xd5,0xff})),
        make_record("/samples/clean.txt", 27U, 1U,
            digest({0x49,0x5d,0x31,0x26,0x18,0x40,0x55,0xbf,0x00,0x07,0x45,0x1b,0x58,0x07,0xf6,0xd2,
                    0x8d,0xa3,0xce,0x05,0xe0,0x66,0xcb,0x9d,0xff,0x2c,0xbd,0xf6,0xe0,0x59,0xc9,0xf4}),
            digest({0x88,0xd7,0x8d,0xd5,0xb7,0x19,0x7c,0xf6,0x0e,0x6f,0x27,0x41,0x22,0xbf,0xb5,0x2c,
                    0x48,0x86,0xec,0xe4,0x02,0x4e,0xaf,0x3e,0x89,0x12,0x41,0xe7,0xb9,0x19,0xcb,0x2b})),
        make_record("/apps/fileio.elf", 5548U, 2U,
            digest({0x42,0x1f,0xb9,0x69,0x98,0x3d,0x79,0x28,0x42,0xd3,0xbd,0x2f,0x0c,0xbd,0xae,0x20,
                    0x73,0x2d,0xbc,0xc3,0xc9,0xbc,0xe5,0xd8,0x23,0xd9,0x01,0xb8,0x6a,0x51,0xb3,0x97}),
            digest({0x5a,0xcc,0x70,0xa7,0x8b,0xd8,0x30,0xcd,0x7b,0x04,0x79,0x9b,0xf3,0xc2,0xbc,0x22,
                    0x90,0x5a,0xc5,0x30,0x70,0xdb,0x00,0xb5,0x35,0x68,0x7c,0x8b,0x70,0x3a,0x93,0x4e})),
    };
}

Bytes build() {
    const auto source = records();
    Bytes output(sizeof(Header) + source.size() * sizeof(Record) + sizeof(kZvrtSignatureV1));
    std::memcpy(output.data(), kZvrtHeaderV1, sizeof(kZvrtHeaderV1));
    std::memcpy(output.data() + sizeof(Header), source.data(), source.size() * sizeof(Record));
    std::memcpy(output.data() + output.size() - sizeof(kZvrtSignatureV1), kZvrtSignatureV1, sizeof(kZvrtSignatureV1));
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
        if (argc != 4) {
            std::cerr << "usage: zvrt-builder <v1.zvrt> <tampered.zvrt> <wrong-key.zvrt>\n";
            return 2;
        }
        auto valid = build();
        auto tampered = valid;
        auto wrong_key = valid;
        tampered[sizeof(Header) + 48U] ^= 0x01U;
        wrong_key[64U] ^= 0xA5U;
        write_all(argv[1], valid);
        write_all(argv[2], tampered);
        write_all(argv[3], wrong_key);
        std::cout << "zvrt-builder: OK schema=1 version=1 records=4 chunk=4096 leaves=1+1+1+2 negative=2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zvrt-builder: " << error.what() << "\n";
        return 1;
    }
}
