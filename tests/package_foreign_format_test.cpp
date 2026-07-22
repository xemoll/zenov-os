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

static std::vector<std::uint8_t> elf_fixture(std::uint8_t elf_class, std::uint8_t endian,
                                             std::uint8_t osabi, std::uint16_t type,
                                             std::uint16_t machine) {
    std::vector<std::uint8_t> bytes(20U, 0U);
    bytes[0] = 0x7fU;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = elf_class;
    bytes[5] = endian;
    bytes[6] = 1U;
    bytes[7] = osabi;
    if (endian == 1U) {
        bytes[16] = static_cast<std::uint8_t>(type & 0xffU);
        bytes[17] = static_cast<std::uint8_t>(type >> 8U);
        bytes[18] = static_cast<std::uint8_t>(machine & 0xffU);
        bytes[19] = static_cast<std::uint8_t>(machine >> 8U);
    } else {
        bytes[16] = static_cast<std::uint8_t>(type >> 8U);
        bytes[17] = static_cast<std::uint8_t>(type & 0xffU);
        bytes[18] = static_cast<std::uint8_t>(machine >> 8U);
        bytes[19] = static_cast<std::uint8_t>(machine & 0xffU);
    }
    return bytes;
}

static std::vector<std::uint8_t> iso_fixture() {
    std::vector<std::uint8_t> bytes(0x8006U, 0U);
    bytes[0x8001] = 'C';
    bytes[0x8002] = 'D';
    bytes[0x8003] = '0';
    bytes[0x8004] = '0';
    bytes[0x8005] = '1';
    return bytes;
}

static std::vector<std::uint8_t> ps_pkg_fixture(std::uint16_t platform) {
    std::vector<std::uint8_t> bytes = {0x7fU, 'P', 'K', 'G', 0x80U, 0U, 0U, 0U};
    bytes[6] = static_cast<std::uint8_t>(platform >> 8U);
    bytes[7] = static_cast<std::uint8_t>(platform & 0xffU);
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
    okay &= check("elf-native", elf_fixture(1U, 1U, 0U, 2U, 3U), "a.elf",
                  Format::elf, Support::host_import);
    okay &= check("elf-x86-64", elf_fixture(2U, 1U, 0U, 2U, 62U), "a.elf",
                  Format::elf_foreign, Support::runtime_required);
    okay &= check("elf-ps2", elf_fixture(1U, 1U, 0U, 2U, 8U), "a.elf",
                  Format::playstation_ps2_elf, Support::runtime_required);
    okay &= check("elf-ps4", elf_fixture(2U, 1U, 9U, 0xfe00U, 62U), "a.elf",
                  Format::playstation_self, Support::runtime_required);
    okay &= check("appimage", {0x7f,'E','L','F'}, "a.AppImage",
                  Format::appimage, Support::runtime_required);

    okay &= check("pe", pe_fixture(), "a.exe",
                  Format::portable_executable, Support::runtime_required);
    okay &= check("mz-without-pe", {'M','Z',0,0}, "a.exe",
                  Format::unknown, Support::unsupported);
    okay &= check("msi", {0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1}, "a.msi",
                  Format::msi, Support::runtime_required);
    okay &= check("msp", {0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1}, "a.msp",
                  Format::msi, Support::runtime_required);
    okay &= check("msix", {'P','K',0x03,0x04}, "a.msix",
                  Format::msix, Support::runtime_required);
    okay &= check("cab", {'M','S','C','F'}, "a.cab",
                  Format::cabinet, Support::inspect_only);
    okay &= check("msu", {'M','S','C','F'}, "a.msu",
                  Format::windows_update, Support::inspect_only);
    okay &= check("wim", {'M','S','W','I','M',0,0,0}, "a.wim",
                  Format::windows_image, Support::inspect_only);

    okay &= check("deb", {'!','<','a','r','c','h','>','\n'}, "a.deb",
                  Format::deb, Support::inspect_only);
    okay &= check("rpm", {0xed,0xab,0xee,0xdb}, "a.rpm",
                  Format::rpm, Support::inspect_only);
    okay &= check("apk-v2", {0x1f,0x8b,0x08,0x00}, "a.apk",
                  Format::alpine_apk, Support::inspect_only);
    okay &= check("apk-v3", {'A','D','B','.', 'p','c','k','g'}, "a.apk",
                  Format::alpine_apk, Support::inspect_only);
    okay &= check("arch-zstd", {0x28,0xb5,0x2f,0xfd}, "a.pkg.tar.zst",
                  Format::arch_package, Support::inspect_only);
    okay &= check("snap", {'h','s','q','s'}, "a.snap",
                  Format::snap, Support::runtime_required);
    okay &= check("flatpak", {0,1,2,3}, "a.flatpak",
                  Format::flatpak, Support::runtime_required, true);

    okay &= check("apple-pkg", {'x','a','r','!'}, "a.pkg",
                  Format::apple_pkg, Support::runtime_required);
    okay &= check("java-not-fat-macho", {0xca,0xfe,0xba,0xbe,0,0,0,61}, "A.class",
                  Format::unknown, Support::unsupported);

    okay &= check("xbox-xbe", {'X','B','E','H'}, "default.xbe",
                  Format::xbox_xbe, Support::runtime_required);
    okay &= check("xbox-xex", {'X','E','X','2'}, "default.xex",
                  Format::xbox_xex, Support::runtime_required);
    okay &= check("xbox-stfs", {'L','I','V','E'}, "content.bin",
                  Format::xbox_stfs, Support::runtime_required);
    okay &= check("xbox-xvc", {0,1,2,3}, "a.xvc",
                  Format::xbox_xvc, Support::partner_only, true);
    okay &= check("xbox-msixvc", {0,1,2,3}, "a.msixvc",
                  Format::xbox_msixvc, Support::partner_only, true);
    okay &= check("xbox-msixvc2", {0,1,2,3}, "a.msixvc2",
                  Format::xbox_msixvc2, Support::partner_only, true);

    okay &= check("psx-exe", {'P','S','-','X',' ','E','X','E'}, "a.exe",
                  Format::playstation_psx_exe, Support::runtime_required);
    okay &= check("psp-pbp", {0,'P','B','P'}, "EBOOT.PBP",
                  Format::playstation_pbp, Support::runtime_required);
    okay &= check("ps3-pkg", ps_pkg_fixture(1U), "a.pkg",
                  Format::playstation_pkg, Support::partner_only);
    okay &= check("psp-vita-pkg", ps_pkg_fixture(2U), "a.pkg",
                  Format::playstation_pkg, Support::partner_only);
    okay &= check("ps4-pkg", {0x7f,'C','N','T'}, "a.pkg",
                  Format::playstation_pkg, Support::partner_only);
    okay &= check("ps-pup", {'S','C','E','U','F'}, "PS3UPDAT.PUP",
                  Format::playstation_pup, Support::partner_only);
    okay &= check("ps3-self", {'S','C','E',0}, "EBOOT.BIN",
                  Format::playstation_self, Support::partner_only);
    okay &= check("ps4-self", {0x4f,0x15,0x3d,0x1d}, "eboot.bin",
                  Format::playstation_self, Support::partner_only);
    okay &= check("vita-vpk", {'P','K',0x03,0x04}, "a.vpk",
                  Format::playstation_pkg, Support::runtime_required);

    okay &= check("iso9660", iso_fixture(), "game.iso",
                  Format::disc_image, Support::runtime_required);
    okay &= check("chd", {'M','C','o','m','p','r','H','D'}, "game.chd",
                  Format::chd_image, Support::runtime_required);
    okay &= check("disc-extension", {0,1,2,3}, "game.cue",
                  Format::disc_image, Support::runtime_required, true);
    okay &= check("generic-zip", {'P','K',0x03,0x04}, "a.zip",
                  Format::zip, Support::inspect_only);
    okay &= check("unknown", {0,1,2,3}, "a.dat",
                  Format::unknown, Support::unsupported);

    if (!okay) return 1;
    std::printf("PACKAGE_FOREIGN_FORMAT_TEST_OK cases=%u generations=legacy-current\n", cases);
    return 0;
}
