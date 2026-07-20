#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../security/zcap_crypto_material.hpp"

namespace {
#pragma pack(push, 1)
struct Record {
    char path[48];
    std::uint32_t mask;
    char read_scope[48];
    char write_scope[48];
    std::uint8_t reserved[12];
};
#pragma pack(pop)
static_assert(sizeof(Record) == 160U);

void copy_canonical(char* output, std::size_t capacity, const std::string& value) {
    if (value.size() >= capacity) throw std::runtime_error("ZCAP string exceeds fixed field");
    std::memset(output, 0, capacity);
    std::memcpy(output, value.data(), value.size());
}
Record profile(const std::string& path, std::uint32_t mask, const std::string& read_scope, const std::string& write_scope) {
    Record output{};
    copy_canonical(output.path, sizeof(output.path), path);
    output.mask = mask;
    copy_canonical(output.read_scope, sizeof(output.read_scope), read_scope);
    copy_canonical(output.write_scope, sizeof(output.write_scope), write_scope);
    return output;
}
std::array<Record, 7> policy_v1() {
    return {{
        profile("/apps/hello.zex", 0x01U, "", ""),
        profile("/apps/fileio.elf", 0x7DU, "/apps/userio.txt", "/apps/userio.txt"),
        profile("/apps/args.elf", 0x05U, "/docs/readme.txt", ""),
        profile("/apps/console.elf", 0x81U, "", ""),
        profile("/apps/protect.elf", 0x01U, "", ""),
        profile("/apps/kaccess.elf", 0x00U, "", ""),
        profile("/apps/zenovapp.zex", 0x01U, "", ""),
    }};
}
std::vector<std::uint8_t> build(const unsigned char header[64], const unsigned char signature[256], const std::array<Record, 7>& records) {
    std::vector<std::uint8_t> output(64U + sizeof(records) + 256U);
    std::memcpy(output.data(), header, 64U);
    std::memcpy(output.data() + 64U, records.data(), sizeof(records));
    std::memcpy(output.data() + output.size() - 256U, signature, 256U);
    return output;
}
void write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open output: " + path);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write output: " + path);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 5) {
            std::cerr << "usage: zcap-builder <v1.zcap> <v2.zcap> <tampered.zcap> <wrong-key.zcap>\n";
            return 2;
        }
        auto v1_records = policy_v1();
        auto v2_records = v1_records;
        v2_records[0].mask = 0U;
        auto v1 = build(kZcapHeaderV1, kZcapSignatureV1, v1_records);
        auto v2 = build(kZcapHeaderV2, kZcapSignatureV2, v2_records);
        auto tampered = v2;
        auto wrong_key = v2;
        tampered[80] ^= 0x01U;
        wrong_key[56] ^= 0xA5U;
        write_file(argv[1], v1);
        write_file(argv[2], v2);
        write_file(argv[3], tampered);
        write_file(argv[4], wrong_key);
        std::cout << "zcap-builder: OK schema=1 pss profiles=7 v1=" << v1.size() << " v2=" << v2.size() << " negative=2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zcap-builder: " << error.what() << "\n";
        return 1;
    }
}
