#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;
#include "../kernel/parts/security_policy_format.inc"

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

template <std::size_t N>
bool parse(const char (&text)[N], uint32_t& value) {
    return security_policy_format::parse_nonzero_decimal_u32(
        reinterpret_cast<const uint8_t*>(text), static_cast<uint32_t>(N - 1U), value);
}

bool reference_component_valid(const char* value, uint32_t begin, uint32_t end) {
    const uint32_t length = end - begin;
    if (!length) return false;
    if (length == 1U && value[begin] == '.') return false;
    return !(length == 2U && value[begin] == '.' && value[begin + 1U] == '.');
}

bool reference_declared_path(const char* value, uint32_t capacity, uint32_t declared, bool allow_empty = false) {
    if (!value || !capacity || declared >= capacity) return false;
    if (value[declared] != 0) return false;
    for (uint32_t i = 0U; i < declared; ++i) if (value[i] == 0) return false;
    for (uint32_t i = declared + 1U; i < capacity; ++i) if (value[i] != 0) return false;
    if (!declared) return allow_empty;
    if (declared < 2U || value[0] != '/' || value[declared - 1U] == '/') return false;

    uint32_t component_begin = 1U;
    for (uint32_t i = 1U; i < declared; ++i) {
        const auto byte = static_cast<uint8_t>(value[i]);
        if (byte < 0x20U || byte > 0x7EU || value[i] == '\\') return false;
        if (value[i] == '/') {
            if (!reference_component_valid(value, component_begin, i)) return false;
            component_begin = i + 1U;
        }
    }
    return reference_component_valid(value, component_begin, declared);
}

void fuzz_declared_paths() {
    uint32_t state = 0x7F4A7C15U;
    uint32_t accepted = 0U, rejected = 0U;
    char data[48]{};
    for (uint32_t call = 0U; call < 400000U; ++call) {
        for (char& byte : data) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            byte = static_cast<char>(state & 0xFFU);
        }
        const uint32_t declared = state % 64U;
        const uint32_t mode = call & 3U;
        if (mode && declared < sizeof(data)) {
            data[declared] = 0;
            for (uint32_t i = declared + 1U; i < sizeof(data); ++i) data[i] = 0;
        }
        if (mode >= 2U && declared >= 2U && declared < sizeof(data)) {
            data[0] = '/';
            for (uint32_t i = 1U; i < declared; ++i) {
                const auto letter = static_cast<uint32_t>((static_cast<std::uint64_t>(state) + i) % 26U);
                data[i] = static_cast<char>('a' + letter);
            }
            if (mode == 3U && declared > 6U) data[declared / 2U] = '/';
        }
        const bool actual = security_policy_format::canonical_absolute_path_with_length(
            data, static_cast<uint32_t>(sizeof(data)), declared, false);
        const bool expected = reference_declared_path(data, static_cast<uint32_t>(sizeof(data)), declared, false);
        if (actual != expected) {
            std::cerr << "policy-path oracle mismatch call=" << call << " declared=" << declared
                      << " mode=" << mode << " actual=" << actual << " expected=" << expected << '\n';
            throw std::runtime_error("declared path oracle mismatch");
        }
        if (actual) ++accepted; else ++rejected;
    }
    require(accepted != 0U && rejected != 0U, "fuzz corpus must cover both verdicts");
    std::cout << "SECURITY_POLICY_PATH_FUZZ_OK calls=400000 oracle=exact accepted=" << accepted
              << " rejected=" << rejected << " modes=raw,terminated,printable,segmented\n";
}
}

int main() {
    try {
        uint32_t value = 0U;
        require(parse("1\n", value) && value == 1U, "parse one");
        require(parse("4294967295\n", value) && value == 0xFFFFFFFFU, "parse max");
        require(!parse("4294967296\n", value), "reject max+1");
        require(!parse("4294967299\n", value), "reject wrapping decimal");
        require(!parse("0\n", value), "reject zero");
        require(!parse("01\n", value), "reject leading zero");
        require(!parse("1", value), "require newline");
        require(!parse("1\n2", value), "reject trailing bytes");
        require(!parse("+1\n", value) && !parse(" 1\n", value), "reject sign or space");
        require(!security_policy_format::parse_nonzero_decimal_u32(nullptr, 2U, value), "reject null");
        uint32_t next = 0U;
        require(security_policy_format::next_version(1U, next) && next == 2U, "next version");
        require(!security_policy_format::next_version(0xFFFFFFFFU, next) && next == 0U, "reject version wrap");

        char valid[48] = "/apps/fileio.elf";
        char empty[48]{};
        char dot_terminal[48] = "/apps/.";
        char dotdot_terminal[48] = "/apps/..";
        char dot_middle[48] = "/apps/./file";
        char dotdot_middle[48] = "/apps/../file";
        char duplicate[48] = "/apps//file";
        char backslash[48] = "/apps\\file";
        char control[48] = "/apps/file"; control[5] = '\x1f';
        char padded[48] = "/apps/file"; padded[20] = 'X';
        char relative[48] = "apps/file";
        char trailing[48] = "/apps/file/";
        char root[48] = "/";
        char high_ascii[48] = "/apps/file"; high_ascii[5] = static_cast<char>(0x80);
        char unterminated[4] = {'/', 'a', 'b', 'c'};
        require(security_policy_format::canonical_absolute_path(valid, sizeof(valid)), "valid path");
        require(security_policy_format::canonical_absolute_path(empty, sizeof(empty), true), "allowed empty scope");
        require(!security_policy_format::canonical_absolute_path(empty, sizeof(empty)), "reject empty path");
        require(!security_policy_format::canonical_absolute_path(dot_terminal, sizeof(dot_terminal)), "reject terminal dot");
        require(!security_policy_format::canonical_absolute_path(dotdot_terminal, sizeof(dotdot_terminal)), "reject terminal dotdot");
        require(!security_policy_format::canonical_absolute_path(dot_middle, sizeof(dot_middle)), "reject middle dot");
        require(!security_policy_format::canonical_absolute_path(dotdot_middle, sizeof(dotdot_middle)), "reject middle dotdot");
        require(!security_policy_format::canonical_absolute_path(duplicate, sizeof(duplicate)), "reject duplicate slash");
        require(!security_policy_format::canonical_absolute_path(backslash, sizeof(backslash)), "reject backslash");
        require(!security_policy_format::canonical_absolute_path(control, sizeof(control)), "reject control");
        require(!security_policy_format::canonical_absolute_path(padded, sizeof(padded)), "reject nonzero padding");
        require(!security_policy_format::canonical_absolute_path(relative, sizeof(relative)), "reject relative path");
        require(!security_policy_format::canonical_absolute_path(trailing, sizeof(trailing)), "reject trailing slash");
        require(!security_policy_format::canonical_absolute_path(root, sizeof(root)), "reject root-only path");
        require(!security_policy_format::canonical_absolute_path(high_ascii, sizeof(high_ascii)), "reject non-ASCII path");
        require(!security_policy_format::canonical_absolute_path(unterminated, sizeof(unterminated)), "reject unterminated path");
        require(string_length(unterminated) == sizeof(unterminated), "bound fixed-array string length");

        char declared_valid[16] = "/apps/file";
        require(security_policy_format::canonical_absolute_path_with_length(declared_valid, sizeof(declared_valid), 10U),
                "accept exact declared length");
        require(!security_policy_format::canonical_absolute_path_with_length(declared_valid, sizeof(declared_valid), 9U),
                "reject short declared length");
        require(!security_policy_format::canonical_absolute_path_with_length(declared_valid, sizeof(declared_valid), 11U),
                "reject long declared length");
        require(!security_policy_format::canonical_absolute_path_with_length(declared_valid, sizeof(declared_valid), sizeof(declared_valid)),
                "reject out-of-range declared length");
        char declared_padded[16] = "/apps/file"; declared_padded[12] = 'X';
        require(!security_policy_format::canonical_absolute_path_with_length(declared_padded, sizeof(declared_padded), 10U),
                "reject nonzero bytes after declared terminator");

        fuzz_declared_paths();
        std::cout << "SECURITY_POLICY_FORMAT_TEST_OK decimal=10 path=16 declared-length=5 bounded-array=yes oracle=exact version-wrap=blocked\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "security-policy-format-test: " << error.what() << '\n';
        return 1;
    }
}
