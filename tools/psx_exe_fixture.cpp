#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 3U) = static_cast<std::uint8_t>(value >> 24U);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: psx-exe-fixture <output.exe>\n";
            return 2;
        }
        constexpr std::uint32_t load = 0x80010000U;
        constexpr std::uint32_t code_bytes = 8U * 4U;
        const std::string message = "PSX_R3000A_HLE_PUTS_OK\n";
        const std::uint32_t payload_size = (code_bytes + static_cast<std::uint32_t>(message.size()) + 1U + 3U) & ~3U;
        std::vector<std::uint8_t> bytes(0x800U + payload_size, 0U);
        const char magic[8] = {'P','S','-','X',' ','E','X','E'};
        for (std::size_t i = 0U; i < 8U; ++i) bytes[i] = static_cast<std::uint8_t>(magic[i]);
        put32(bytes, 0x10U, load);
        put32(bytes, 0x18U, load);
        put32(bytes, 0x1cU, payload_size);
        put32(bytes, 0x30U, 0x801FFF00U);

        const std::uint32_t words[] = {
            0x3C048001U,             // lui   a0,0x8001
            0x34840020U,             // ori   a0,a0,0x20
            0x2409003EU,             // addiu t1,zero,0x3e (A0 puts)
            0x241900A0U,             // addiu t9,zero,0xa0
            0x0320F809U,             // jalr  ra,t9
            0x00000000U,             // delay slot
            0x24040000U,             // addiu a0,zero,0
            0x0000000DU              // break: diagnostic sandbox exit
        };
        for (std::size_t i = 0U; i < sizeof(words) / sizeof(words[0]); ++i)
            put32(bytes, 0x800U + i * 4U, words[i]);
        for (std::size_t i = 0U; i < message.size(); ++i)
            bytes[0x800U + code_bytes + i] = static_cast<std::uint8_t>(message[i]);

        std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create output");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error("cannot write output");
        std::cout << "PSX_EXE_FIXTURE_OK bytes=" << bytes.size()
                  << " entry=0x80010000 hle=A0:3e exit=break\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "psx-exe-fixture: " << error.what() << '\n';
        return 1;
    }
}
