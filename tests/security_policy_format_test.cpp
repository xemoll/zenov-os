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
        std::cout << "SECURITY_POLICY_FORMAT_TEST_OK decimal=10 path=16 version-wrap=blocked\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "security-policy-format-test: " << error.what() << '\n';
        return 1;
    }
}
