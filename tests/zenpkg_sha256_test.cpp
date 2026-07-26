// SPDX-License-Identifier: BSD-2-Clause

#include "../tools/zenpkg/sha256.hpp"

#include <cstdio>
#include <string_view>

namespace {

bool expect_digest(std::string_view label, std::string_view input,
                   std::string_view expected) {
    const std::string actual = zenpkg::sha256_hex(input);
    if (actual == expected) return true;
    std::fprintf(stderr, "sha256 vector=%.*s expected=%.*s actual=%s\n",
                 static_cast<int>(label.size()), label.data(),
                 static_cast<int>(expected.size()), expected.data(), actual.c_str());
    return false;
}

} // namespace

int main() {
    bool okay = true;
    okay &= expect_digest(
        "empty", "",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    okay &= expect_digest(
        "abc", "abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    okay &= expect_digest(
        "multi-block",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    zenpkg::Sha256 streaming;
    streaming.update(std::string_view{"a"});
    streaming.update(std::string_view{"b"});
    streaming.update(std::string_view{"c"});
    const auto streaming_digest = streaming.final();
    const std::string streaming_hex =
        zenpkg::hex_encode(streaming_digest.data(), streaming_digest.size());
    if (streaming_hex !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::fprintf(stderr, "sha256 streaming expected=abc-vector actual=%s\n",
                     streaming_hex.c_str());
        okay = false;
    }

    if (!okay) return 1;
    std::puts("ZENPKG_SHA256_TEST_OK vectors=3 streaming=1 explicit_mod32=1");
    return 0;
}
