#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using uint64_t = std::uint64_t;

#include "../kernel/parts/tpm2_protocol.inc"

namespace {
[[noreturn]] void fail(const char* message) {
    std::cerr << "TPM2_PROTOCOL_TEST_FAILED: " << message << '\n';
    std::exit(1);
}
void require(bool condition, const char* message) { if (!condition) fail(message); }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8U) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24U) | (static_cast<uint32_t>(p[1]) << 16U) |
           (static_cast<uint32_t>(p[2]) << 8U) | p[3];
}
void put16(uint8_t* p, uint16_t value) { p[0] = static_cast<uint8_t>(value >> 8U); p[1] = static_cast<uint8_t>(value); }
void put32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value >> 24U); p[1] = static_cast<uint8_t>(value >> 16U);
    p[2] = static_cast<uint8_t>(value >> 8U); p[3] = static_cast<uint8_t>(value);
}
void put64(uint8_t* p, uint64_t value) { put32(p, static_cast<uint32_t>(value >> 32U)); put32(p + 4, static_cast<uint32_t>(value)); }

void verify_command(const uint8_t* command, uint32_t size, uint16_t tag, uint32_t code) {
    require(size >= 10U, "command too short");
    require(be16(command) == tag, "wrong command tag");
    require(be32(command + 2U) == size, "wrong command size");
    require(be32(command + 6U) == code, "wrong command code");
}
}

int main() {
    std::array<uint8_t, 1024> command{};
    uint32_t size = 0U;

    require(tpm2_protocol::build_startup(command.data(), command.size(), size), "startup build");
    require(size == 12U, "startup size");
    verify_command(command.data(), size, tpm2_protocol::st_no_sessions, tpm2_protocol::cc_startup);
    require(be16(command.data() + 10U) == tpm2_protocol::su_clear, "startup mode");

    require(tpm2_protocol::build_nv_read_public(command.data(), command.size(), size), "read-public build");
    require(size == 14U, "read-public size");
    verify_command(command.data(), size, tpm2_protocol::st_no_sessions, tpm2_protocol::cc_nv_read_public);
    require(be32(command.data() + 10U) == tpm2_protocol::nv_index, "read-public index");

    require(tpm2_protocol::build_nv_define_counter(command.data(), command.size(), size), "define build");
    require(size == 45U, "define size");
    verify_command(command.data(), size, tpm2_protocol::st_sessions, tpm2_protocol::cc_nv_define_space);
    require(be32(command.data() + 10U) == tpm2_protocol::rh_owner, "define owner");
    require(be32(command.data() + 14U) == 9U, "define auth size");
    require(be32(command.data() + 18U) == tpm2_protocol::rs_pw, "define password session");
    require(be16(command.data() + 29U) == 14U, "define public size");
    require(be32(command.data() + 31U) == tpm2_protocol::nv_index, "define index");
    require(be16(command.data() + 35U) == tpm2_protocol::alg_sha256, "define name alg");
    require(be32(command.data() + 37U) == tpm2_protocol::nv_attributes, "define attributes");
    require(be16(command.data() + 43U) == 8U, "define data bytes");

    require(tpm2_protocol::build_nv_increment(command.data(), command.size(), size), "increment build");
    require(size == 31U, "increment size");
    verify_command(command.data(), size, tpm2_protocol::st_sessions, tpm2_protocol::cc_nv_increment);

    require(tpm2_protocol::build_nv_read_counter(command.data(), command.size(), size), "read build");
    require(size == 35U, "read size");
    verify_command(command.data(), size, tpm2_protocol::st_sessions, tpm2_protocol::cc_nv_read);
    require(be16(command.data() + 31U) == 8U && be16(command.data() + 33U) == 0U, "read parameters");

    std::array<uint8_t, 80> public_response{};
    const uint32_t public_size = 10U + 2U + 14U + 2U + 34U;
    put16(public_response.data(), tpm2_protocol::st_no_sessions);
    put32(public_response.data() + 2U, public_size);
    put32(public_response.data() + 6U, 0U);
    put16(public_response.data() + 10U, 14U);
    put32(public_response.data() + 12U, tpm2_protocol::nv_index);
    put16(public_response.data() + 16U, tpm2_protocol::alg_sha256);
    put32(public_response.data() + 18U, tpm2_protocol::nv_attributes);
    put16(public_response.data() + 22U, 0U);
    put16(public_response.data() + 24U, 8U);
    put16(public_response.data() + 26U, 34U);
    public_response[28] = 0x00U; public_response[29] = 0x0BU;

    tpm2_protocol::CounterPublic parsed{};
    uint32_t response_code = 0U;
    require(tpm2_protocol::parse_counter_public(public_response.data(), public_size, parsed, response_code) ==
            tpm2_protocol::PublicValidation::okay, "public parse");
    require(parsed.index == tpm2_protocol::nv_index && parsed.data_size == 8U, "public fields");

    auto wrong_attributes = public_response;
    put32(wrong_attributes.data() + 18U, tpm2_protocol::nv_attributes ^ 0x2U);
    require(tpm2_protocol::parse_counter_public(wrong_attributes.data(), public_size, parsed, response_code) ==
            tpm2_protocol::PublicValidation::wrong_attributes, "attribute rejection");

    auto malformed_public = public_response;
    put16(malformed_public.data() + 10U, 0x7FFFU);
    require(tpm2_protocol::parse_counter_public(malformed_public.data(), public_size, parsed, response_code) ==
            tpm2_protocol::PublicValidation::malformed, "public bounds");

    std::array<uint8_t, 40> read_response{};
    constexpr uint64_t expected_counter = 0x0000000100000002ULL;
    const uint32_t read_size = 10U + 4U + 2U + 8U + 5U;
    put16(read_response.data(), tpm2_protocol::st_sessions);
    put32(read_response.data() + 2U, read_size);
    put32(read_response.data() + 6U, 0U);
    put32(read_response.data() + 10U, 10U);
    put16(read_response.data() + 14U, 8U);
    put64(read_response.data() + 16U, expected_counter);
    put16(read_response.data() + 24U, 0U);
    read_response[26U] = 0U;
    put16(read_response.data() + 27U, 0U);
    uint64_t counter = 0U;
    require(tpm2_protocol::parse_counter_value(read_response.data(), read_size, counter, response_code), "counter parse");
    require(counter == expected_counter, "counter value");

    auto truncated = read_response;
    put32(truncated.data() + 2U, read_size + 1U);
    require(!tpm2_protocol::parse_counter_value(truncated.data(), read_size, counter, response_code), "truncated rejection");

    std::array<uint8_t, 10> missing{};
    put16(missing.data(), tpm2_protocol::st_no_sessions);
    put32(missing.data() + 2U, 10U);
    put32(missing.data() + 6U, tpm2_protocol::rc_handle_1);
    require(tpm2_protocol::parse_counter_public(missing.data(), missing.size(), parsed, response_code) ==
            tpm2_protocol::PublicValidation::response_error, "missing response");
    require(tpm2_protocol::missing_nv_index_code(response_code), "missing handle code");

    std::cout << "TPM2_PROTOCOL_TEST_OK commands=5 endian=canonical bounds=closed nv-public=exact counter=64bit auth=password-empty\n";
    return 0;
}
