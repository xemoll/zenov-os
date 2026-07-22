#include <cstdint>
#include <cstdio>
#include <vector>

#include "../kernel/parts/package_foreign_format.inc"

using package_foreign::Format;
using package_foreign::Support;

static bool expect(const char* label, const std::vector<std::uint8_t>& bytes, const char* path,
                   Format format, Support support, bool extension_only = false) {
    const auto detected = package_foreign::detect(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), path);
    if (detected.format != format || detected.support != support ||
        detected.extension_only != extension_only) {
        std::fprintf(stderr, "case=%s format=%u support=%u extension=%u\n", label,
                     static_cast<unsigned>(detected.format),
                     static_cast<unsigned>(detected.support),
                     detected.extension_only ? 1U : 0U);
        return false;
    }
    return true;
}

static std::vector<std::uint8_t> pe_fixture() {
    std::vector<std::uint8_t> bytes(68U, 0U);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[0x3c] = 0x40U;
    bytes[0x40] = 'P';
    bytes[0x41] = 'E';
    return bytes;
}

int main() {
    unsigned cases = 0;
    auto check = [&](const char* label, std::vector<std::uint8_t> bytes, const char* path,
                     Format format, Support support, bool extension_only = false) {
        ++cases;
        return expect(label, bytes, path, format, support, extension_only);
    };

    bool okay = true;
    okay &= check("zenpkg", {'Z','E','N','P','K','G','1',0}, "a.zpk",
                  Format::zenpkg, Support::installable);
    okay &= check("zex1", {'Z','E','X','1'}, "a.zex",
                  Format::zex1, Support::host_import);
    okay &= check("elf", {0x7f,'E','L','F'}, "a.elf",
                  Format::elf, Support::host_import);
    okay &= check("appimage", {0x7f,'E','L','F'}, "a.AppImage",
                  Format::appimage, Support::runtime_required);
    okay &= check("pe", pe_fixture(), "a.exe",
                  Format::portable_executable, Support::runtime_required);
    okay &= check("mz-without-pe", {'M','Z',0,0}, "a.exe",
                  Format::unknown, Support::unsupported);
    okay &= check("msi", {0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1}, "a.msi",
                  Format::msi, Support::runtime_required);
    okay &= check("msix", {'P','K',0x03,0x04}, "a.msix",
                  Format::msix, Support::runtime_required);
    okay &= check("deb", {'!','<','a','r','c','h','>','\n'}, "a.deb",
                  Format::deb, Support::inspect_only);
    okay &= check("rpm", {0xed,0xab,0xee,0xdb}, "a.rpm",
                  Format::rpm, Support::inspect_only);
    okay &= check("apple-pkg", {'x','a','r','!'}, "a.pkg",
                  Format::apple_pkg, Support::runtime_required);
    okay &= check("java-not-fat-macho", {0xca,0xfe,0xba,0xbe,0,0,0,61}, "A.class",
                  Format::unknown, Support::unsupported);
    okay &= check("xbox-xvc", {0,1,2,3}, "a.xvc",
                  Format::xbox_xvc, Support::partner_only, true);
    okay &= check("playstation", {0x7f,'C','N','T'}, "a.pkg",
                  Format::playstation_pkg, Support::partner_only);
    okay &= check("flatpak", {0,1,2,3}, "a.flatpak",
                  Format::flatpak, Support::runtime_required, true);
    okay &= check("unknown", {0,1,2,3}, "a.bin",
                  Format::unknown, Support::unsupported);

    if (!okay) return 1;
    std::printf("PACKAGE_FOREIGN_FORMAT_TEST_OK cases=%u\n", cases);
    return 0;
}
