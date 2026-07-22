const char* foreign_format_text(package_foreign::Format format) {
    using package_foreign::Format;
    switch (format) {
        case Format::zenpkg: return "zenpkg";
        case Format::zex1: return "zex1";
        case Format::elf: return "elf";
        case Format::elf_foreign: return "elf-foreign";
        case Format::appimage: return "appimage";
        case Format::portable_executable: return "pe";
        case Format::msi: return "msi";
        case Format::msix: return "msix";
        case Format::cabinet: return "cab";
        case Format::windows_update: return "msu";
        case Format::windows_image: return "wim";
        case Format::deb: return "deb";
        case Format::rpm: return "rpm";
        case Format::alpine_apk: return "alpine-apk";
        case Format::arch_package: return "arch-package";
        case Format::snap: return "snap";
        case Format::flatpak: return "flatpak";
        case Format::apple_pkg: return "apple-pkg";
        case Format::apple_dmg: return "apple-dmg";
        case Format::macho: return "macho";
        case Format::xbox_xbe: return "xbox-xbe";
        case Format::xbox_xex: return "xbox-xex";
        case Format::xbox_stfs: return "xbox-stfs";
        case Format::xbox_xvc: return "xbox-xvc";
        case Format::xbox_msixvc: return "xbox-msixvc";
        case Format::xbox_msixvc2: return "xbox-msixvc2";
        case Format::playstation_psx_exe: return "playstation-psx-exe";
        case Format::playstation_ps2_elf: return "playstation-ps2-elf";
        case Format::playstation_pbp: return "playstation-pbp";
        case Format::playstation_pkg: return "playstation-pkg";
        case Format::playstation_pup: return "playstation-pup";
        case Format::playstation_self: return "playstation-self";
        case Format::disc_image: return "disc-image";
        case Format::chd_image: return "chd";
        case Format::zip: return "zip";
        case Format::unknown: return "unknown";
    }
    return "unknown";
}

void formats() {
    console::line("  FORMAT/FAMILY                  0.1.1 STATUS");
    console::line("  ----------------------------- ------------------------------------------");
    console::line("  ZenPkg, ZEX1, ELF32/i386      native verify/import/sign/install path");
    console::line("  DEB/RPM/APK/pkg.tar/CAB/WIM   inspect-only; package scripts never run");
    console::line("  AppImage/Snap/Flatpak         runtime-required; Linux ABI is absent");
    console::line("  PE/MSI/MSIX/AppX              runtime-required; Win32/UWP ABI is absent");
    console::line("  PKG/DMG/Mach-O                runtime-required; Darwin ABI is absent");
    console::line("  XBE/XEX/STFS                  legacy Xbox runtime-required");
    console::line("  XVC/MSIXVC/MSIXVC2            partner-only; GDK identity/keys required");
    console::line("  PS-X/R5900 ELF/PBP/discs      legacy console runtime-required");
    console::line("  Generic MIPS ELF              foreign runtime; not assumed to be PS2");
    console::line("  PlayStation PKG/PUP/SELF      signed/encrypted partner boundary");
    console::line("");
    console::line("  Detection never grants execution authority or bypasses ZenRepo policy.");
    serial::line("ZENPKG_FORMATS_OK generations=legacy-current");
}

bool probe_format(const char* path) {
    if (!path || !*path) return false;
    storage::FileInfo info{};
    if (!storage::query(path, info) || info.type != storage::zfs_type_file) {
        console::warning("Package probe could not read that file.");
        serial::line("ZENPKG_PROBE_IO_ERROR");
        return false;
    }
    if (info.size > maximum_package_bytes) {
        console::warning("Package probe exceeds the bounded 64 KiB input window.");
        serial::line("ZENPKG_PROBE_TOO_LARGE");
        return false;
    }
    uint32_t actual = 0;
    if (!storage::read_file(path, package_buffer, maximum_package_bytes, actual) || actual != info.size) {
        console::warning("Package probe encountered a short read.");
        serial::line("ZENPKG_PROBE_IO_ERROR");
        return false;
    }

    const package_foreign::Detection detection = package_foreign::classify(package_buffer, actual, path);
    console::write("  Format        ");
    console::line(foreign_format_text(detection.format));
    console::write("  Name          ");
    console::line(detection.name);
    console::write("  Family        ");
    console::line(detection.family);
    console::write("  Support       ");
    console::line(package_foreign::support_text(detection.support));
    console::write("  Confidence    ");
    console::line(detection.extension_only ? "extension-only" : "signature");
    console::write("  Bytes         ");
    console::unsigned_dec(actual);
    console::put('\n');
    console::write("  Reason        ");
    console::line(detection.reason);

    if (detection.support == package_foreign::Support::unsupported) {
        serial::line("ZENPKG_PROBE_UNSUPPORTED");
        return false;
    }
    serial::write("ZENPKG_PROBE_OK format=");
    serial::write(foreign_format_text(detection.format));
    serial::write(" support=");
    serial::write(package_foreign::support_text(detection.support));
    serial::write(" extension=");
    serial::line(detection.extension_only ? "1" : "0");
    return true;
}
