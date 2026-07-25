namespace storage {
struct FileInfo { uint32_t type, size, checksum; };

FsResult query_result(const char* input, FileInfo& info) {
    if (!zfs_mounted) return fs_failure(FsOperation::query, FsStatus::not_mounted);
    if (!input || !*input) return fs_failure(FsOperation::query, FsStatus::invalid_argument);
    char path[48];
    if (!normalize_path(input, path)) return fs_failure(FsOperation::query, FsStatus::invalid_path);
    if (string_equal(path, "/")) {
        info = FileInfo{zfs_type_directory, 0U, 0U};
        return fs_success(FsOperation::query);
    }
    const int32_t index = find_entry(path);
    if (index < 0) return fs_failure(FsOperation::query, FsStatus::not_found);
    const ZfsEntry& entry = zfs_entries[index];
    if (entry.type != zfs_type_file && entry.type != zfs_type_directory) {
        return fs_failure(FsOperation::query, FsStatus::metadata_corrupt);
    }
    info.type = entry.type;
    info.size = entry.size;
    info.checksum = entry.checksum;
    return fs_success(FsOperation::query);
}

bool query(const char* input, FileInfo& info) { return query_result(input, info).ok(); }

FsResult sync_metadata_result() {
    if (!zfs_mounted) return fs_failure(FsOperation::sync, FsStatus::not_mounted);
    const FsResult recovered = recover_transactions_result();
    if (!recovered.ok()) return recovered;
    if (zfs_super.generation == 0xFFFFFFFFU) {
        return fs_failure(FsOperation::sync, FsStatus::metadata_corrupt);
    }
    const uint8_t* source = reinterpret_cast<const uint8_t*>(zfs_entries);
    for (uint32_t sector = 0; sector < zfs_super.entry_sectors; ++sector) {
        const BlockResult write = zfs_write_sector(1U + sector, source + sector * sector_size);
        if (!write.ok()) return zfs_block_failure(FsOperation::sync, write);
    }
    ZfsSuperblock committed = zfs_super;
    ++committed.generation;
    const BlockResult super_write = zfs_write_sector(
        0U, reinterpret_cast<const uint8_t*>(&committed));
    if (!super_write.ok()) return zfs_block_failure(FsOperation::sync, super_write);
    zfs_super = committed;
    return fs_success(FsOperation::sync, zfs_super.generation);
}

bool sync_metadata() { return sync_metadata_result().ok(); }

bool valid_stored_path(const char* path) {
    if (!path || path[0] != '/') return false;
    uint32_t length = 0U;
    while (length < 48U && path[length]) ++length;
    if (length < 2U || length >= 48U || path[length - 1U] == '/') return false;
    for (uint32_t i = 0; i < length; ++i) {
        const char value = path[i];
        if (value < 32 || value > 126 ||
            (value == '/' && i + 1U < length && path[i + 1U] == '/')) {
            return false;
        }
    }
    return true;
}

FsResult check_integrity_result(bool emit_details) {
    if (!zfs_mounted || !file_buffer) {
        return fs_failure(FsOperation::query, FsStatus::not_mounted);
    }
    const FsResult recovered = recover_transactions_result();
    if (!recovered.ok()) return recovered;
    uint32_t files = 0U, directories = 0U;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        const ZfsEntry& entry = zfs_entries[i];
        if (!entry.used) continue;
        if ((entry.type != zfs_type_file && entry.type != zfs_type_directory) ||
            entry.flags || !valid_stored_path(entry.path)) {
            if (emit_details) console::warning("Invalid ZenovFS entry metadata.");
            return fs_failure(FsOperation::query, FsStatus::metadata_corrupt);
        }
        for (uint32_t other = i + 1U; other < zfs_super.entry_count; ++other) {
            if (zfs_entries[other].used &&
                zfs_entries[other].type != zfs_type_transaction &&
                string_equal(entry.path, zfs_entries[other].path)) {
                if (emit_details) console::warning("Duplicate ZenovFS path detected.");
                return fs_failure(FsOperation::query, FsStatus::metadata_corrupt);
            }
        }
        if (!parent_exists(entry.path)) {
            if (emit_details) console::warning("ZenovFS entry has a missing parent directory.");
            return fs_failure(FsOperation::query, FsStatus::metadata_corrupt);
        }
        if (entry.type == zfs_type_directory) {
            if (entry.size || entry.checksum) {
                return fs_failure(FsOperation::query, FsStatus::metadata_corrupt);
            }
            ++directories;
            continue;
        }
        if (entry.size > max_file_bytes()) {
            return fs_failure(FsOperation::query, FsStatus::metadata_corrupt);
        }
        uint32_t loaded = 0U;
        const FsResult read = read_file_result(entry.path, file_buffer,
                                               max_file_bytes(), loaded);
        if (!read.ok() || loaded != entry.size) {
            if (emit_details) {
                console::write("ZenovFS verification failed: ");
                console::line(fs_status_name(read.status));
            }
            return fs_result(FsOperation::query, read.status, read.bytes, read.block);
        }
        ++files;
    }
    if (emit_details) {
        console::write("  Files checked  "); console::unsigned_dec(files); console::put('\n');
        console::write("  Directories    "); console::unsigned_dec(directories); console::put('\n');
        console::write("  Generation     "); console::unsigned_dec(zfs_super.generation); console::put('\n');
        console::success("ZENOVFS_FSCK_OK");
    }
    return fs_success(FsOperation::query, files + directories);
}

[[maybe_unused]] bool check_integrity(bool emit_details) {
    return check_integrity_result(emit_details).ok();
}

void print_integrity() {
    const FsResult result = check_integrity_result(true);
    if (result.ok()) return;
    console::write("ZENOVFS_FSCK_FAILED status=");
    console::line(fs_status_name(result.status));
}
} // namespace storage
