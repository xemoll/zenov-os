bool flush_entry(uint32_t index) { if (index >= zfs_super.entry_count || !zfs_read_sector(entry_sector(index), sector_buffer).ok()) return false; memcpy(sector_buffer + entry_slot_offset(index), &zfs_entries[index], sizeof(ZfsEntry)); return zfs_write_sector(entry_sector(index), sector_buffer).ok(); }
bool load_entries() { uint8_t* destination = reinterpret_cast<uint8_t*>(zfs_entries); for (uint32_t sector = 0; sector < zfs_super.entry_sectors; ++sector) if (!zfs_read_sector(1U + sector, destination + sector * sector_size).ok()) return false; return true; }
bool recover_transactions() {
    bool recovered = false;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        if (zfs_entries[i].used && zfs_entries[i].type == zfs_type_transaction) {
            memset(&zfs_entries[i], 0, sizeof(ZfsEntry));
            if (!flush_entry(i)) return false;
            recovered = true;
        }
    }
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        ZfsEntry& committed = zfs_entries[i];
        if (!committed.used || committed.type != zfs_type_file || !(committed.flags & zfs_flag_committed)) continue;
        const uint32_t old = committed.reserved;
        if (old < zfs_super.entry_count && old != i && zfs_entries[old].used) {
            memset(&zfs_entries[old], 0, sizeof(ZfsEntry));
            if (!flush_entry(old)) return false;
        }
        committed.flags &= static_cast<uint16_t>(~zfs_flag_committed);
        committed.reserved = 0;
        if (!flush_entry(i)) return false;
        recovered = true;
    }
    if (recovered) serial::line("ZENOVFS_INTERRUPTED_WRITE_RECOVERED");
    return true;
}
bool mount() {
    zfs_mounted = false;
    if (!ata0.present || !zfs_read_sector(0U, reinterpret_cast<uint8_t*>(&zfs_super)).ok()) return false;
    const char expected[8] = {'Z','E','N','O','V','F','S','1'};
    if (!bytes_equal(zfs_super.magic, expected, 8) || zfs_super.version != 1 || !zfs_super.entry_count || zfs_super.entry_count > zfs_max_entries || zfs_super.total_sectors > ata0.sectors || !zfs_super.slot_sectors) return false;
    if (!load_entries()) return false;
    file_buffer = static_cast<uint8_t*>(heap::allocate(max_file_bytes(), 16));
    if (!file_buffer || !recover_transactions()) return false;
    zfs_mounted = true;
    string_copy(current_directory, "/", sizeof(current_directory));
    return true;
}

bool read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    actual_size = 0;
    if (!zfs_mounted || !output) return false;
    char path[48];
    if (!normalize_path(input, path)) return false;
    const int32_t found = find_entry(path);
    if (found < 0 || zfs_entries[found].type != zfs_type_file || zfs_entries[found].size > capacity) return false;
    const uint32_t size = zfs_entries[found].size;
    uint32_t copied = 0;
    for (uint32_t sector = 0; copied < size; ++sector) {
        if (!zfs_read_sector(data_sector(static_cast<uint32_t>(found)) + sector, sector_buffer).ok()) return false;
        uint32_t chunk = size - copied;
        if (chunk > sector_size) chunk = sector_size;
        memcpy(output + copied, sector_buffer, chunk);
        copied += chunk;
    }
    if (fnv1a(output, size) != zfs_entries[found].checksum) return false;
    actual_size = size;
    return true;
}
bool write_file(const char* input, const uint8_t* data, uint32_t size, bool append) {
    if (!zfs_mounted || (!data && size)) return false;
    char path[48];
    if (!normalize_path(input, path) || string_equal(path, "/") || !parent_exists(path)) return false;
    const int32_t old_index = find_entry(path);
    uint32_t existing = 0;
    if (old_index >= 0) {
        if (zfs_entries[old_index].type != zfs_type_file) return false;
        existing = append ? zfs_entries[old_index].size : 0;
    }
    if (existing > max_file_bytes() || size > max_file_bytes() - existing) return false;
    if (append && existing) {
        uint32_t loaded = 0;
        if (!read_file(path, file_buffer, max_file_bytes(), loaded) || loaded != existing) return false;
    }
    if (size && data != file_buffer + existing) memcpy(file_buffer + existing, data, size);
    const uint32_t total = existing + size;
    const int32_t staging_index = free_entry(old_index >= 0 ? static_cast<uint32_t>(old_index) : transaction_none);
    if (staging_index < 0) return false;
    uint32_t written = 0;
    for (uint32_t sector = 0; written < total; ++sector) {
        memset(sector_buffer, 0, sizeof(sector_buffer));
        uint32_t chunk = total - written;
        if (chunk > sector_size) chunk = sector_size;
        memcpy(sector_buffer, file_buffer + written, chunk);
        if (!zfs_write_sector(data_sector(static_cast<uint32_t>(staging_index)) + sector, sector_buffer).ok()) return false;
        written += chunk;
    }
    ZfsEntry& stage = zfs_entries[staging_index];
    memset(&stage, 0, sizeof(stage));
    stage.used = 1; stage.type = zfs_type_transaction; stage.size = total; stage.checksum = fnv1a(file_buffer, total);
    stage.reserved = old_index >= 0 ? static_cast<uint32_t>(old_index) : transaction_none;
    if (!path_copy(stage.path, path, sizeof(stage.path)) || !flush_entry(static_cast<uint32_t>(staging_index))) return false;
    stage.type = zfs_type_file; stage.flags = zfs_flag_committed;
    if (!flush_entry(static_cast<uint32_t>(staging_index))) return false;
    if (old_index >= 0) {
        memset(&zfs_entries[old_index], 0, sizeof(ZfsEntry));
        if (!flush_entry(static_cast<uint32_t>(old_index))) return true;
    }
    stage.flags = 0; stage.reserved = 0;
    flush_entry(static_cast<uint32_t>(staging_index));
    return true;
}
bool make_directory(const char* input) { if (!zfs_mounted) return false; char path[48]; if (!normalize_path(input, path) || string_equal(path, "/") || find_entry(path) >= 0 || !parent_exists(path)) return false; const int32_t index = free_entry(); if (index < 0) return false; ZfsEntry& entry = zfs_entries[index]; memset(&entry, 0, sizeof(ZfsEntry)); entry.used = 1; entry.type = zfs_type_directory; return path_copy(entry.path, path, sizeof(entry.path)) && flush_entry(static_cast<uint32_t>(index)); }
bool touch(const char* input) { char path[48]; if (!normalize_path(input, path)) return false; const int32_t found = find_entry(path); return found >= 0 ? zfs_entries[found].type == zfs_type_file : write_file(path, nullptr, 0, false); }
bool has_children(const char* path) { const uint32_t length = static_cast<uint32_t>(string_length(path)); for (uint32_t i = 0; i < zfs_super.entry_count; ++i) if (zfs_entries[i].used && zfs_entries[i].type != zfs_type_transaction && string_starts_with(zfs_entries[i].path, path) && zfs_entries[i].path[length] == '/' && zfs_entries[i].path[length + 1]) return true; return false; }
bool remove(const char* input) { if (!zfs_mounted) return false; char path[48]; if (!normalize_path(input, path) || string_equal(path, "/")) return false; const int32_t index = find_entry(path); if (index < 0 || (zfs_entries[index].type == zfs_type_directory && has_children(path))) return false; memset(&zfs_entries[index], 0, sizeof(ZfsEntry)); return flush_entry(static_cast<uint32_t>(index)); }
bool change_directory(const char* input) { if (!zfs_mounted) return false; char path[48]; if (!normalize_path(input, path)) return false; if (!string_equal(path, "/")) { const int32_t index = find_entry(path); if (index < 0 || zfs_entries[index].type != zfs_type_directory) return false; } return path_copy(current_directory, path, sizeof(current_directory)); }
bool rename_entry(const char* source_input, const char* destination_input) { char source[48], destination[48]; if (!normalize_path(source_input, source) || !normalize_path(destination_input, destination) || string_equal(source, "/") || find_entry(destination) >= 0 || !parent_exists(destination)) return false; const int32_t index = find_entry(source); if (index < 0 || (zfs_entries[index].type == zfs_type_directory && has_children(source))) return false; return path_copy(zfs_entries[index].path, destination, sizeof(zfs_entries[index].path)) && flush_entry(static_cast<uint32_t>(index)); }
bool copy_file(const char* source, const char* destination) { uint32_t size = 0; return read_file(source, file_buffer, max_file_bytes(), size) && write_file(destination, file_buffer, size, false); }
