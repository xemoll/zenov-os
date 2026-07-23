namespace package_foreign {

enum class Format : uint8_t {
    unknown,
    zenpkg,
    zex1,
    elf,
    elf_foreign,
    appimage,
    portable_executable,
    msi,
    msix,
    cabinet,
    windows_update,
    windows_image,
    deb,
    rpm,
    alpine_apk,
    arch_package,
    snap,
    flatpak,
    apple_pkg,
    apple_dmg,
    macho,
    xbox_xbe,
    xbox_xex,
    xbox_stfs,
    xbox_xvc,
    xbox_msixvc,
    xbox_msixvc2,
    playstation_psx_exe,
    playstation_ps2_elf,
    playstation_pbp,
    playstation_pkg,
    playstation_pup,
    playstation_self,
    disc_image,
    chd_image,
    zip
};

enum class Support : uint8_t {
    installable,
    host_import,
    runtime_required,
    partner_only,
    inspect_only,
    unsupported
};

struct Detection {
    Format format;
    Support support;
    const char* name;
    const char* family;
    const char* reason;
    bool extension_only;
};

char folded(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool suffix_folded(const char* path, const char* suffix) {
    if (!path || !suffix) return false;
    uint32_t path_size = 0;
    uint32_t suffix_size = 0;
    while (path[path_size]) ++path_size;
    while (suffix[suffix_size]) ++suffix_size;
    if (suffix_size > path_size) return false;
    const uint32_t offset = path_size - suffix_size;
    for (uint32_t i = 0; i < suffix_size; ++i) {
        if (folded(path[offset + i]) != folded(suffix[i])) return false;
    }
    return true;
}

bool bytes_at(const uint8_t* bytes, uint32_t size, uint32_t offset,
              const uint8_t* expected, uint32_t expected_size) {
    if (!bytes || !expected || offset > size || expected_size > size - offset) return false;
    uint8_t difference = 0;
    for (uint32_t i = 0; i < expected_size; ++i) {
        difference |= static_cast<uint8_t>(bytes[offset + i] ^ expected[i]);
    }
    return difference == 0;
}

uint16_t u16_le_at(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& valid) {
    if (!bytes || offset > size || 2U > size - offset) {
        valid = false;
        return 0;
    }
    valid = true;
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1U]) << 8U);
}

uint16_t u16_be_at(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& valid) {
    if (!bytes || offset > size || 2U > size - offset) {
        valid = false;
        return 0;
    }
    valid = true;
    return static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset]) << 8U) |
           static_cast<uint16_t>(bytes[offset + 1U]);
}

uint32_t u32_le_at(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& valid) {
    if (!bytes || offset > size || 4U > size - offset) {
        valid = false;
        return 0;
    }
    valid = true;
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<uint32_t>(bytes[offset + 3U]) << 24U);
}

uint32_t u32_be_at(const uint8_t* bytes, uint32_t size, uint32_t offset, bool& valid) {
    if (!bytes || offset > size || 4U > size - offset) {
        valid = false;
        return 0;
    }
    valid = true;
    return (static_cast<uint32_t>(bytes[offset]) << 24U) |
           (static_cast<uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<uint32_t>(bytes[offset + 3U]);
}

Detection result(Format format, Support support, const char* name,
                 const char* family, const char* reason, bool extension_only = false) {
    return Detection{format, support, name, family, reason, extension_only};
}

Detection detect(const uint8_t* bytes, uint32_t size, const char* path) {
    static const uint8_t zenpkg_magic[8] = {'Z','E','N','P','K','G','1',0};
    static const uint8_t zex1_magic[4] = {'Z','E','X','1'};
    static const uint8_t elf_magic[4] = {0x7f,'E','L','F'};
    static const uint8_t rpm_magic[4] = {0xed,0xab,0xee,0xdb};
    static const uint8_t ar_magic[8] = {'!','<','a','r','c','h','>','\n'};
    static const uint8_t zip_magic[4] = {'P','K',0x03,0x04};
    static const uint8_t zip_empty_magic[4] = {'P','K',0x05,0x06};
    static const uint8_t zip_spanned_magic[4] = {'P','K',0x07,0x08};
    static const uint8_t xar_magic[4] = {'x','a','r','!'};
    static const uint8_t squashfs_magic[4] = {'h','s','q','s'};
    static const uint8_t cfb_magic[8] = {0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};
    static const uint8_t mz_magic[2] = {'M','Z'};
    static const uint8_t pe_magic[4] = {'P','E',0,0};
    static const uint8_t cab_magic[4] = {'M','S','C','F'};
    static const uint8_t wim_magic[8] = {'M','S','W','I','M',0,0,0};
    static const uint8_t gzip_magic[2] = {0x1f,0x8b};
    static const uint8_t zstd_magic[4] = {0x28,0xb5,0x2f,0xfd};
    static const uint8_t xz_magic[6] = {0xfd,0x37,0x7a,0x58,0x5a,0x00};
    static const uint8_t bzip_magic[3] = {'B','Z','h'};
    static const uint8_t lz4_magic[4] = {0x04,0x22,0x4d,0x18};
    static const uint8_t apk_adb_magic[4] = {'A','D','B','.'};
    static const uint8_t xbe_magic[4] = {'X','B','E','H'};
    static const uint8_t xex_magic[4] = {'X','E','X','2'};
    static const uint8_t xbox_con_magic[4] = {'C','O','N',' '};
    static const uint8_t xbox_live_magic[4] = {'L','I','V','E'};
    static const uint8_t xbox_pirs_magic[4] = {'P','I','R','S'};
    static const uint8_t psx_exe_magic[8] = {'P','S','-','X',' ','E','X','E'};
    static const uint8_t pbp_magic[4] = {0,'P','B','P'};
    static const uint8_t ps_pkg_magic[4] = {0x7f,'P','K','G'};
    static const uint8_t ps4_pkg_magic[4] = {0x7f,'C','N','T'};
    static const uint8_t pup_magic[5] = {'S','C','E','U','F'};
    static const uint8_t ps3_self_magic[4] = {'S','C','E',0};
    static const uint8_t ps4_self_magic[4] = {0x4f,0x15,0x3d,0x1d};
    static const uint8_t dmg_magic[4] = {'k','o','l','y'};
    static const uint8_t iso_magic[5] = {'C','D','0','0','1'};
    static const uint8_t chd_magic[8] = {'M','C','o','m','p','r','H','D'};

    if (bytes_at(bytes, size, 0, zenpkg_magic, sizeof(zenpkg_magic))) {
        return result(Format::zenpkg, Support::installable, "ZenPkg ZENPKG1", "ZenovOS",
                      "verified container; installation still requires signed repository authorization");
    }
    if (bytes_at(bytes, size, 0, zex1_magic, sizeof(zex1_magic))) {
        return result(Format::zex1, Support::host_import, "ZEX1 executable", "ZenovOS",
                      "raw payload can be wrapped by zenpkg import-native before repository signing");
    }

    if (bytes_at(bytes, size, 0, xbe_magic, sizeof(xbe_magic))) {
        return result(Format::xbox_xbe, Support::runtime_required, "Original Xbox XBE executable", "Xbox",
                      "legacy executable is recognized; Xbox kernel, graphics, audio and input runtime are absent");
    }
    if (bytes_at(bytes, size, 0, xex_magic, sizeof(xex_magic))) {
        return result(Format::xbox_xex, Support::runtime_required, "Xbox 360 XEX2 executable", "Xbox 360",
                      "legacy executable is recognized; Xenon/PowerPC runtime and Xbox services are absent");
    }
    if (bytes_at(bytes, size, 0, xbox_con_magic, sizeof(xbox_con_magic)) ||
        bytes_at(bytes, size, 0, xbox_live_magic, sizeof(xbox_live_magic)) ||
        bytes_at(bytes, size, 0, xbox_pirs_magic, sizeof(xbox_pirs_magic))) {
        return result(Format::xbox_stfs, Support::runtime_required, "Xbox 360 STFS content container", "Xbox 360",
                      "content container is recognized; license metadata is preserved and no activation bypass is performed");
    }
    if (bytes_at(bytes, size, 0, psx_exe_magic, sizeof(psx_exe_magic))) {
        return result(Format::playstation_psx_exe, Support::runtime_required, "PS-X EXE", "PlayStation",
                      "PlayStation executable is recognized; MIPS R3000A and console services are absent");
    }
    if (bytes_at(bytes, size, 0, pbp_magic, sizeof(pbp_magic))) {
        return result(Format::playstation_pbp, Support::runtime_required, "PlayStation PBP container", "PSP/PlayStation",
                      "PBP container is recognized; PSP/PS1 runtime and content licensing remain external");
    }
    if (bytes_at(bytes, size, 0, pup_magic, sizeof(pup_magic))) {
        return result(Format::playstation_pup, Support::partner_only, "PlayStation system update package", "PlayStation",
                      "firmware/update container is recognized; official validation and target hardware are required");
    }
    if (bytes_at(bytes, size, 0, ps3_self_magic, sizeof(ps3_self_magic))) {
        return result(Format::playstation_self, Support::partner_only, "PlayStation SELF executable", "PS3/PS Vita",
                      "signed/encrypted executable is recognized; keys, signatures and console runtime are not bypassed");
    }
    if (bytes_at(bytes, size, 0, ps4_self_magic, sizeof(ps4_self_magic))) {
        return result(Format::playstation_self, Support::partner_only, "PlayStation 4 SELF executable", "PlayStation 4",
                      "SELF header is recognized; signed/encrypted segments require lawful platform tooling and runtime");
    }

    if (bytes_at(bytes, size, 0, elf_magic, sizeof(elf_magic))) {
        if (suffix_folded(path, ".appimage")) {
            return result(Format::appimage, Support::runtime_required, "AppImage", "Linux",
                          "ELF AppImage is recognized; ZenovOS has no FUSE/dynamic Linux runtime");
        }
        if (size >= 20U) {
            const uint8_t elf_class = bytes[4];
            const uint8_t elf_data = bytes[5];
            bool machine_valid = false;
            const uint16_t machine = elf_data == 1U
                ? u16_le_at(bytes, size, 18U, machine_valid)
                : u16_be_at(bytes, size, 18U, machine_valid);
            if (machine_valid && elf_class == 1U && elf_data == 1U && machine == 3U) {
                return result(Format::elf, Support::host_import, "ELF32/i386 executable", "Linux/Unix",
                              "only static ET_EXEC images satisfying the ZenovOS loader contract can be imported");
            }
            if (machine_valid && elf_class == 1U && machine == 8U) {
                return result(Format::playstation_ps2_elf, Support::runtime_required, "MIPS ELF executable", "PlayStation 2/MIPS",
                              "MIPS ELF is recognized; Emotion Engine/IOP runtime and console services are absent");
            }
            if (machine_valid && elf_class == 2U && elf_data == 1U && machine == 62U && size >= 18U) {
                bool type_valid = false;
                const uint16_t type = u16_le_at(bytes, size, 16U, type_valid);
                if (type_valid && bytes[7] == 9U &&
                    (type == 0xfe00U || type == 0xfe10U || type == 0xfe18U)) {
                    return result(Format::playstation_self, Support::runtime_required, "PlayStation 4 ELF", "PlayStation 4",
                                  "decrypted SCE ELF is recognized; FreeBSD-derived PS4 ABI and services are absent");
                }
            }
        }
        return result(Format::elf_foreign, Support::runtime_required, "foreign ELF executable", "Linux/Unix/console",
                      "ELF architecture or ABI is not the native static ELF32/i386 import target");
    }

    if (bytes_at(bytes, size, 0, cfb_magic, sizeof(cfb_magic)) &&
        (suffix_folded(path, ".msi") || suffix_folded(path, ".msp") || suffix_folded(path, ".msm"))) {
        return result(Format::msi, Support::runtime_required, "Windows Installer database", "Windows",
                      "MSI/MSP/MSM compound document is recognized; installer actions and Win32 execution are unavailable");
    }
    if (bytes_at(bytes, size, 0, mz_magic, sizeof(mz_magic))) {
        bool offset_valid = false;
        const uint32_t pe_offset = u32_le_at(bytes, size, 0x3cU, offset_valid);
        if (offset_valid && bytes_at(bytes, size, pe_offset, pe_magic, sizeof(pe_magic))) {
            return result(Format::portable_executable, Support::runtime_required, "PE/COFF executable", "Windows",
                          "MZ and PE signatures are valid; a Win32 compatibility runtime is still required");
        }
    }

    const bool zip = bytes_at(bytes, size, 0, zip_magic, sizeof(zip_magic)) ||
                     bytes_at(bytes, size, 0, zip_empty_magic, sizeof(zip_empty_magic)) ||
                     bytes_at(bytes, size, 0, zip_spanned_magic, sizeof(zip_spanned_magic));
    if (zip && (suffix_folded(path, ".msix") || suffix_folded(path, ".appx") ||
                suffix_folded(path, ".msixbundle") || suffix_folded(path, ".appxbundle"))) {
        return result(Format::msix, Support::runtime_required, "MSIX/AppX package", "Windows",
                      "ZIP package family is recognized; deployment identity, signatures and Win32/UWP runtime are absent");
    }
    if (zip && suffix_folded(path, ".vpk")) {
        return result(Format::playstation_pkg, Support::runtime_required, "PlayStation Vita VPK", "PlayStation Vita",
                      "ZIP-based VPK is recognized; Vita ABI and content policy remain external");
    }
    if (bytes_at(bytes, size, 0, cab_magic, sizeof(cab_magic))) {
        if (suffix_folded(path, ".msu")) {
            return result(Format::windows_update, Support::inspect_only, "Windows Update Standalone package", "Windows",
                          "CAB-based MSU is recognized; Windows servicing stack actions are never executed");
        }
        return result(Format::cabinet, Support::inspect_only, "Microsoft Cabinet archive", "Windows",
                      "CAB archive is recognized; setup directives and executable payloads are never run");
    }
    if (bytes_at(bytes, size, 0, wim_magic, sizeof(wim_magic))) {
        return result(Format::windows_image, Support::inspect_only, "Windows Imaging Format image", "Windows",
                      "WIM image is recognized; Windows deployment and servicing are not implemented");
    }

    if (bytes_at(bytes, size, 0, ar_magic, sizeof(ar_magic)) && suffix_folded(path, ".deb")) {
        return result(Format::deb, Support::inspect_only, "Debian binary package", "Linux",
                      "ar package is recognized; maintainer scripts and shared-library dependencies are not executed");
    }
    if (bytes_at(bytes, size, 0, rpm_magic, sizeof(rpm_magic))) {
        return result(Format::rpm, Support::inspect_only, "RPM package", "Linux",
                      "RPM lead is recognized; scripts, database semantics and dynamic dependencies are unavailable");
    }
    if (bytes_at(bytes, size, 0, apk_adb_magic, sizeof(apk_adb_magic)) && suffix_folded(path, ".apk")) {
        return result(Format::alpine_apk, Support::inspect_only, "Alpine APK v3 package", "Linux/Alpine",
                      "ADB package header is recognized; signatures, scripts and filesystem deployment are not executed");
    }
    if (bytes_at(bytes, size, 0, gzip_magic, sizeof(gzip_magic)) && suffix_folded(path, ".apk")) {
        return result(Format::alpine_apk, Support::inspect_only, "Alpine APK v2 package", "Linux/Alpine",
                      "gzip-stream APK v2 candidate is recognized; signatures and install scripts are not executed");
    }
    const bool arch_suffix = suffix_folded(path, ".pkg.tar.zst") || suffix_folded(path, ".pkg.tar.xz") ||
                             suffix_folded(path, ".pkg.tar.gz") || suffix_folded(path, ".pkg.tar.bz2") ||
                             suffix_folded(path, ".pkg.tar.lz4");
    const bool arch_compression = bytes_at(bytes, size, 0, zstd_magic, sizeof(zstd_magic)) ||
                                  bytes_at(bytes, size, 0, xz_magic, sizeof(xz_magic)) ||
                                  bytes_at(bytes, size, 0, gzip_magic, sizeof(gzip_magic)) ||
                                  bytes_at(bytes, size, 0, bzip_magic, sizeof(bzip_magic)) ||
                                  bytes_at(bytes, size, 0, lz4_magic, sizeof(lz4_magic));
    if (arch_suffix && arch_compression) {
        return result(Format::arch_package, Support::inspect_only, "Arch Linux ALPM package", "Linux/Arch",
                      "compressed pkg.tar package is recognized; ALPM scripts, signatures and dependency actions are not executed");
    }
    if (bytes_at(bytes, size, 0, squashfs_magic, sizeof(squashfs_magic)) && suffix_folded(path, ".snap")) {
        return result(Format::snap, Support::runtime_required, "Snap package", "Linux",
                      "SquashFS Snap payload is recognized; snapd, namespaces and Linux runtime are absent");
    }

    if (bytes_at(bytes, size, 0, xar_magic, sizeof(xar_magic)) && suffix_folded(path, ".pkg")) {
        return result(Format::apple_pkg, Support::runtime_required, "macOS installer package", "macOS",
                      "XAR installer is recognized; Installer scripts, Mach-O and Apple trust services are absent");
    }
    if (size >= 512U && bytes_at(bytes, size, size - 512U, dmg_magic, sizeof(dmg_magic))) {
        return result(Format::apple_dmg, Support::runtime_required, "Apple disk image (UDIF)", "macOS",
                      "UDIF trailer is recognized; HFS/APFS mounting and Mach-O runtime are absent");
    }
    if (size >= 4U) {
        bool valid = false;
        const uint32_t magic = u32_be_at(bytes, size, 0, valid);
        if (valid && (magic == 0xfeedfaceU || magic == 0xcefaedfeU ||
                      magic == 0xfeedfacfU || magic == 0xcffaedfeU)) {
            return result(Format::macho, Support::runtime_required, "Mach-O executable", "macOS",
                          "thin Mach-O is recognized; Darwin ABI, loader and Apple frameworks are absent");
        }
        if (valid && (magic == 0xcafebabeU || magic == 0xbebafecaU) && size >= 8U) {
            bool count_valid = false;
            const uint32_t count = magic == 0xcafebabeU
                ? u32_be_at(bytes, size, 4U, count_valid)
                : u32_le_at(bytes, size, 4U, count_valid);
            if (count_valid && count >= 1U && count <= 32U) {
                return result(Format::macho, Support::runtime_required, "Mach-O universal binary", "macOS",
                              "fat Mach-O header is plausible; Darwin ABI and Apple frameworks are absent");
            }
        }
    }

    if (bytes_at(bytes, size, 0, ps_pkg_magic, sizeof(ps_pkg_magic))) {
        bool platform_valid = false;
        const uint16_t platform = u16_be_at(bytes, size, 6U, platform_valid);
        if (platform_valid && platform == 1U) {
            return result(Format::playstation_pkg, Support::partner_only, "PlayStation 3 PKG", "PlayStation 3",
                          "PS3 package header is recognized; encryption, licenses and signatures are not bypassed");
        }
        if (platform_valid && platform == 2U) {
            return result(Format::playstation_pkg, Support::partner_only, "PSP/PlayStation Vita PKG", "PSP/PlayStation Vita",
                          "portable PlayStation package is recognized; encryption and content licenses remain external");
        }
        return result(Format::playstation_pkg, Support::partner_only, "PlayStation PKG", "PlayStation",
                      "package signature is recognized but platform metadata is unknown or truncated");
    }
    if (bytes_at(bytes, size, 0, ps4_pkg_magic, sizeof(ps4_pkg_magic))) {
        return result(Format::playstation_pkg, Support::partner_only, "PlayStation 4 package", "PlayStation 4",
                      "PS4 package signature is recognized; official keys, licenses and target runtime are required");
    }

    if (size > 0x8005U && bytes_at(bytes, size, 0x8001U, iso_magic, sizeof(iso_magic))) {
        return result(Format::disc_image, Support::runtime_required, "ISO 9660 disc image", "optical media/console",
                      "disc filesystem is recognized; title-specific console runtime and lawful media ownership are required");
    }
    if (bytes_at(bytes, size, 0, chd_magic, sizeof(chd_magic))) {
        return result(Format::chd_image, Support::runtime_required, "CHD compressed media image", "arcade/console media",
                      "compressed media container is recognized; CHD decoder and target runtime are absent");
    }

    if (suffix_folded(path, ".msixvc2")) {
        return result(Format::xbox_msixvc2, Support::partner_only, "MSIXVC2 PC game package", "Xbox GDK/Windows PC",
                      "extension-only match; April 2026 preview format requires GDK tooling, package identity and keys", true);
    }
    if (suffix_folded(path, ".msixvc")) {
        return result(Format::xbox_msixvc, Support::partner_only, "Xbox/PC MSIXVC package", "Xbox GDK/Windows PC",
                      "extension-only match; official GDK identity and package key material are required", true);
    }
    if (suffix_folded(path, ".xvc")) {
        return result(Format::xbox_xvc, Support::partner_only, "Xbox XVC package", "Xbox One/Series",
                      "extension-only match; official GDK identity and package key material are required", true);
    }
    if (suffix_folded(path, ".flatpak") || suffix_folded(path, ".flatpakref")) {
        return result(Format::flatpak, Support::runtime_required, "Flatpak package/reference", "Linux",
                      "extension-only match; OSTree/Flatpak and Linux sandbox runtime are absent", true);
    }
    if (suffix_folded(path, ".dmg")) {
        return result(Format::apple_dmg, Support::runtime_required, "Apple disk image candidate", "macOS",
                      "extension-only match; a valid UDIF trailer was not present in the bounded input", true);
    }
    if (suffix_folded(path, ".pkg")) {
        return result(Format::apple_pkg, Support::runtime_required, "PKG container candidate", "Apple/PlayStation",
                      "extension-only match is ambiguous; a known container signature is required", true);
    }
    if (suffix_folded(path, ".iso") || suffix_folded(path, ".bin") || suffix_folded(path, ".cue")) {
        return result(Format::disc_image, Support::runtime_required, "disc image candidate", "optical media/console",
                      "extension-only match; filesystem or track metadata was not present in the bounded input", true);
    }
    if (zip) {
        return result(Format::zip, Support::inspect_only, "ZIP archive", "portable",
                      "generic archive is recognized but has no executable package contract");
    }
    return result(Format::unknown, Support::unsupported, "unknown", "unknown",
                  "no supported package, executable or media signature was found");
}

const char* support_text(Support support) {
    switch (support) {
        case Support::installable: return "installable";
        case Support::host_import: return "host-import";
        case Support::runtime_required: return "runtime-required";
        case Support::partner_only: return "partner-only";
        case Support::inspect_only: return "inspect-only";
        case Support::unsupported: return "unsupported";
    }
    return "unsupported";
}

} // namespace package_foreign
