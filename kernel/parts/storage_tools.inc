namespace storage {
struct FileInfo {
    uint32_t type;
    uint32_t size;
    uint32_t checksum;
};

bool query(const char* input, FileInfo& info) {
    if (!zfs_mounted || !input || !*input) return false;
    char path[48];
    if (!normalize_path(input, path)) return false;
    if (string_equal(path, "/")) {
        info = FileInfo{2, 0, 0};
        return true;
    }
    const int32_t index = find_entry(path);
    if (index < 0) return false;
    info.type = zfs_entries[index].type;
    info.size = zfs_entries[index].size;
    info.checksum = zfs_entries[index].checksum;
    return true;
}

bool sync_metadata() {
    if (!zfs_mounted) return false;
    if (!ata0.write_sector(0, reinterpret_cast<const uint8_t*>(&zfs_super))) return false;
    const uint8_t* source = reinterpret_cast<const uint8_t*>(zfs_entries);
    for (uint32_t sector = 0; sector < zfs_super.entry_sectors; ++sector) {
        if (!ata0.write_sector(1U + sector, source + sector * sector_size)) return false;
    }
    return true;
}

bool valid_stored_path(const char* path) {
    if (!path || path[0] != '/' || path[1] == 0 || string_length(path) >= sizeof(ZfsEntry::path)) return false;
    for (uint32_t i = 0; path[i]; ++i) {
        const char value = path[i];
        if (value >= 'A' && value <= 'Z') return false;
        if (value == '/' && path[i + 1] == '/') return false;
    }
    return true;
}

bool check_integrity(bool emit_details) {
    if (!zfs_mounted || !file_buffer) return false;
    uint32_t files = 0;
    uint32_t directories = 0;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        const ZfsEntry& entry = zfs_entries[i];
        if (!entry.used) continue;
        if ((entry.type != 1 && entry.type != 2) || !valid_stored_path(entry.path)) {
            if (emit_details) console::warning("Invalid ZenovFS entry metadata.");
            return false;
        }
        for (uint32_t other = i + 1; other < zfs_super.entry_count; ++other) {
            if (zfs_entries[other].used && string_equal(entry.path, zfs_entries[other].path)) {
                if (emit_details) console::warning("Duplicate ZenovFS path detected.");
                return false;
            }
        }
        if (!parent_exists(entry.path)) {
            if (emit_details) console::warning("ZenovFS entry has a missing parent directory.");
            return false;
        }
        if (entry.type == 2) {
            if (entry.size != 0 || entry.checksum != 0) return false;
            ++directories;
            continue;
        }
        if (entry.size > max_file_bytes()) return false;
        uint32_t loaded = 0;
        if (!read_file(entry.path, file_buffer, max_file_bytes(), loaded) || loaded != entry.size) {
            if (emit_details) console::warning("ZenovFS file checksum verification failed.");
            return false;
        }
        ++files;
    }
    if (emit_details) {
        console::write("  Files checked  "); console::unsigned_dec(files); console::put('\n');
        console::write("  Directories    "); console::unsigned_dec(directories); console::put('\n');
        console::write("  Generation     "); console::unsigned_dec(zfs_super.generation); console::put('\n');
        console::success("ZENOVFS_FSCK_OK");
    }
    return true;
}

void print_integrity() {
    if (!check_integrity(true)) console::warning("ZENOVFS_FSCK_FAILED");
}
}
