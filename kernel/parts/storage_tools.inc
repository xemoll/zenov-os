namespace storage {
struct FileInfo { uint32_t type, size, checksum; };
bool query(const char* input, FileInfo& info) {
    if (!zfs_mounted || !input || !*input) return false; char path[48]; if (!normalize_path(input, path)) return false;
    if (string_equal(path, "/")) { info = FileInfo{zfs_type_directory, 0, 0}; return true; }
    const int32_t index = find_entry(path); if (index < 0) return false;
    info.type = zfs_entries[index].type; info.size = zfs_entries[index].size; info.checksum = zfs_entries[index].checksum; return true;
}
bool sync_metadata() {
    if (!zfs_mounted || !recover_transactions()) return false;
    const uint8_t* source = reinterpret_cast<const uint8_t*>(zfs_entries);
    for (uint32_t sector = 0; sector < zfs_super.entry_sectors; ++sector) if (!ata0.write_sector(1U + sector, source + sector * sector_size)) return false;
    ZfsSuperblock committed = zfs_super; ++committed.generation;
    if (!ata0.write_sector(0, reinterpret_cast<const uint8_t*>(&committed))) return false;
    zfs_super = committed; return true;
}
bool valid_stored_path(const char* path) {
    if (!path || path[0] != '/') return false; uint32_t length = 0; while (length < 48U && path[length]) ++length;
    if (length < 2U || length >= 48U || path[length - 1U] == '/') return false;
    for (uint32_t i = 0; i < length; ++i) { const char value = path[i]; if (value < 32 || value > 126 || (value == '/' && i + 1U < length && path[i + 1U] == '/')) return false; }
    return true;
}
bool check_integrity(bool emit_details) {
    if (!zfs_mounted || !file_buffer || !recover_transactions()) return false; uint32_t files = 0, directories = 0;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        const ZfsEntry& entry = zfs_entries[i]; if (!entry.used) continue;
        if ((entry.type != zfs_type_file && entry.type != zfs_type_directory) || entry.flags || !valid_stored_path(entry.path)) { if (emit_details) console::warning("Invalid ZenovFS entry metadata."); return false; }
        for (uint32_t other = i + 1; other < zfs_super.entry_count; ++other) if (zfs_entries[other].used && zfs_entries[other].type != zfs_type_transaction && string_equal(entry.path, zfs_entries[other].path)) { if (emit_details) console::warning("Duplicate ZenovFS path detected."); return false; }
        if (!parent_exists(entry.path)) { if (emit_details) console::warning("ZenovFS entry has a missing parent directory."); return false; }
        if (entry.type == zfs_type_directory) { if (entry.size || entry.checksum) return false; ++directories; continue; }
        if (entry.size > max_file_bytes()) return false; uint32_t loaded = 0;
        if (!read_file(entry.path, file_buffer, max_file_bytes(), loaded) || loaded != entry.size) { if (emit_details) console::warning("ZenovFS file checksum verification failed."); return false; }
        ++files;
    }
    if (emit_details) { console::write("  Files checked  "); console::unsigned_dec(files); console::put('\n'); console::write("  Directories    "); console::unsigned_dec(directories); console::put('\n'); console::write("  Generation     "); console::unsigned_dec(zfs_super.generation); console::put('\n'); console::success("ZENOVFS_FSCK_OK"); }
    return true;
}
void print_integrity() { if (!check_integrity(true)) console::warning("ZENOVFS_FSCK_FAILED"); }
} // namespace storage
