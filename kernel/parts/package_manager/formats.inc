const char* foreign_format_text(package_foreign::Format format) {
    using package_foreign::Format;
    switch (format) {
        case Format::zenpkg: return "zenpkg";
        case Format::zex1: return "zex1";
        case Format::elf: return "elf";
        case Format::appimage: return "appimage";
        case Format::portable_executable: return "pe";
        case Format::msi: return "msi";
        case Format::msix: return "msix";
        case Format::deb: return "deb";
        case Format::rpm: return "rpm";
        case Format::snap: return "snap";
        case Format::flatpak: return "flatpak";
        case Format::apple_pkg: return "apple-pkg";
        case Format::apple_dmg: return "apple-dmg";
        case Format::macho: return "macho";
        case Format::xbox_xvc: return "xbox-xvc";
        case Format::xbox_msixvc: return "xbox-msixvc";
        case Format::playstation_pkg: return "playstation-pkg";
        case Format::zip: return "zip";
        case Format::unknown: return "unknown";
    }
    return "unknown";
}

void formats() {
    console::line("  FORMAT/FAMILY                  0.1.1 STATUS");
    console::line("  ----------------------------- ------------------------------------------");
    console::line("  ZenPkg ZENPKG1                installable after signed authorization");
    console::line("  ZEX1, static ELF32/i386       host-import into ZenPkg, then sign/install");
    console::line("  DEB, RPM, generic ZIP         inspect-only; scripts are never executed");
    console::line("  AppImage, Snap, Flatpak       runtime-required; Linux ABI is absent");
    console::line("  PE, MSI, MSIX/AppX            runtime-required; Win32/UWP ABI is absent");
    console::line("  PKG, DMG, Mach-O              runtime-required; Darwin ABI is absent");
    console::line("  Xbox XVC/MSIXVC               partner-only; GDK identity/keys required");
    console::line("  PlayStation package           partner-only; official SDK/keys required");
    console::line("");
    console::line("  Detection never grants execution authority or bypasses ZenRepo policy.");
    serial::line("ZENPKG_FORMATS_OK");
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

    const package_foreign::Detection detection = package_foreign::detect(package_buffer, actual, path);
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
