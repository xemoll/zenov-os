// SPDX-License-Identifier: BSD-2-Clause

#include "../tools/zenpkg/foreign_probe.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;
unsigned cases = 0;

void expect(bool condition, const char* label) {
    ++cases;
    if (condition) return;
    std::fprintf(stderr, "ZENPKG_NATIVE_SNAPSHOT_TEST_FAIL case=%s\n", label);
    ++failures;
}

bool error_contains(const zenpkg::Error& error, const char* text) {
    return std::string(error.what()).find(text) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: zenpkg-native-snapshot-test HELLO.ZEX OUT\n");
        return 2;
    }

    const std::filesystem::path source = argv[1];
    const std::filesystem::path out = argv[2];
    std::error_code ec;
    std::filesystem::remove_all(out, ec);
    ec.clear();
    std::filesystem::create_directories(out, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create output directory: %s\n", ec.message().c_str());
        return 2;
    }

    const auto snapshot_input = out / "snapshot-input.zex";
    std::filesystem::copy_file(source, snapshot_input,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "cannot copy fixture: %s\n", ec.message().c_str());
        return 2;
    }

    const auto snapshot = zenpkg::require_native_import_candidate(snapshot_input);
    expect(snapshot.detection.format == package_foreign::Format::zex1,
           "snapshot-format");
    expect(snapshot.sha256 == zenpkg::sha256_hex(snapshot.bytes),
           "snapshot-digest");

    std::filesystem::remove(snapshot_input, ec);
    expect(!ec && !std::filesystem::exists(snapshot_input),
           "source-removed-before-import");

    const auto package_path = out / "snapshot.zpk";
    const auto package = zenpkg::import_native(
        snapshot, "snapshot-app", "0.1.0", "BSD-2-Clause",
        "snapshot-test", "redistributable", package_path);
    expect(std::filesystem::exists(package_path), "snapshot-package-written");
    expect(zenpkg::hex_encode(package.payload_digest.data(), package.payload_digest.size()) ==
               snapshot.sha256,
           "snapshot-package-digest");

    auto tampered = snapshot;
    if (!tampered.bytes.empty()) tampered.bytes.back() ^= 0xffU;
    const auto tampered_path = out / "tampered.zpk";
    bool tamper_rejected = false;
    try {
        (void)zenpkg::import_native(
            tampered, "tampered-app", "0.1.0", "BSD-2-Clause",
            "snapshot-test", "redistributable", tampered_path);
    } catch (const zenpkg::Error& error) {
        tamper_rejected = error_contains(error, "snapshot digest mismatch");
    }
    expect(tamper_rejected, "tampered-snapshot-rejected");
    expect(!std::filesystem::exists(tampered_path), "tampered-output-absent");

    const auto oversized_path = out / "oversized.zex";
    {
        std::ofstream stream(oversized_path, std::ios::binary | std::ios::trunc);
        stream.seekp(static_cast<std::streamoff>(zenpkg::zenov_package_limit));
        stream.put('\0');
        stream.flush();
        if (!stream) {
            std::fprintf(stderr, "cannot create oversized fixture\n");
            return 2;
        }
    }
    bool oversized_rejected = false;
    try {
        (void)zenpkg::require_native_import_candidate(oversized_path);
    } catch (const zenpkg::Error& error) {
        oversized_rejected = error_contains(error, "exceeds the current 64 KiB package limit");
    }
    expect(oversized_rejected, "oversized-rejected-before-allocation");

    if (failures != 0) return 1;
    std::printf("ZENPKG_NATIVE_SNAPSHOT_TEST_OK cases=%u bounded=1 digest=1 path-independent=1\n",
                cases);
    return 0;
}
