namespace package_foreign {

enum class Format : uint8_t {
    unknown,
    zenpkg,
    zex1,
    elf,
    appimage,
    portable_executable,
    msi,
    msix,
    deb,
    rpm,
    snap,
    flatpak,
    apple_pkg,
    apple_dmg,
    macho,
    xbox_xvc,
    xbox_msixvc,
    playstation_pkg,
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
    static const uint8_t ps3_pkg_magic[4] = {0x7f,'P','K','G'};
    static const uint8_t ps4_pkg_magic[4] = {0x7f,'C','N','T'};
    static const uint8_t dmg_magic[4] = {'k','o','l','y'};

    if (bytes_at(bytes, size, 0, zenpkg_magic, sizeof(zenpkg_magic))) {
        return result(Format::zenpkg, Support::installable, "ZenPkg ZENPKG1", "ZenovOS",
                      "verified container; installation still requires signed repository authorization");
    }
    if (bytes_at(bytes, size, 0, zex1_magic, sizeof(zex1_magic))) {
        return result(Format::zex1, Support::host_import, "ZEX1 executable", "ZenovOS",
                      "raw payload can be wrapped by zenpkg import-native before repository signing");
    }
    if (bytes_at(bytes, size, 0, elf_magic, sizeof(elf_magic))) {
        if (suffix_folded(path, ".appimage")) {
            return result(Format::appimage, Support::runtime_required, "AppImage", "Linux",
                          "ELF AppImage is recognized; ZenovOS has no FUSE/dynamic Linux runtime");
        }
        return result(Format::elf, Support::host_import, "ELF executable", "Linux/Unix",
                      "only static ELF32/i386 executables can be imported into native ZenPkg");
    }
    if (bytes_at(bytes, size, 0, cfb_magic, sizeof(cfb_magic)) && suffix_folded(path, ".msi")) {
        return result(Format::msi, Support::runtime_required, "Windows Installer MSI", "Windows",
                      "CFB/MSI container is recognized; MSI actions and Win32 execution are not available");
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
                      "ZIP package family is recognized; Win32/UWP runtime and deployment services are absent");
    }
    if (bytes_at(bytes, size, 0, ar_magic, sizeof(ar_magic)) && suffix_folded(path, ".deb")) {
        return result(Format::deb, Support::inspect_only, "Debian binary package", "Linux",
                      "ar package is recognized; maintainer scripts and shared-library dependencies are not executed");
    }
    if (bytes_at(bytes, size, 0, rpm_magic, sizeof(rpm_magic))) {
        return result(Format::rpm, Support::inspect_only, "RPM package", "Linux",
                      "RPM lead is recognized; scripts, database semantics and dynamic dependencies are unavailable");
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
    if (bytes_at(bytes, size, 0, ps3_pkg_magic, sizeof(ps3_pkg_magic)) ||
        bytes_at(bytes, size, 0, ps4_pkg_magic, sizeof(ps4_pkg_magic))) {
        return result(Format::playstation_pkg, Support::partner_only, "PlayStation package signature", "PlayStation",
                      "known package signature; official partner SDK, licensing, keys and target runtime are required");
    }
    if (suffix_folded(path, ".msixvc")) {
        return result(Format::xbox_msixvc, Support::partner_only, "Xbox/PC MSIXVC package", "Xbox",
                      "extension-only match; official GDK identity and package key material are required", true);
    }
    if (suffix_folded(path, ".xvc")) {
        return result(Format::xbox_xvc, Support::partner_only, "Xbox XVC package", "Xbox",
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
    if (zip) {
        return result(Format::zip, Support::inspect_only, "ZIP archive", "portable",
                      "generic archive is recognized but has no executable package contract");
    }
    return result(Format::unknown, Support::unsupported, "unknown", "unknown",
                  "no supported package or executable signature was found");
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
