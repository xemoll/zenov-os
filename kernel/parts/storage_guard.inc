namespace storage {
bool boot_integrity_verified = false;

void verify_boot_integrity() {
    boot_integrity_verified = false;
    if (!zfs_mounted) {
        serial::line("ZENOVFS_BOOT_FSCK_SKIPPED");
        return;
    }
    if (!check_integrity(false)) {
        zfs_mounted = false;
        string_copy(current_directory, "/", sizeof(current_directory));
        serial::line("ZENOVFS_BOOT_FSCK_FAILED");
        console::warning("Persistent volume failed boot-time integrity verification and was disabled.");
        return;
    }
    boot_integrity_verified = true;
    serial::line("ZENOVFS_BOOT_FSCK_OK");
}
}
