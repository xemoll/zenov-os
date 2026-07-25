FsResult zfs_block_failure(FsOperation operation, const BlockResult& block, uint32_t bytes = 0U) {
    zfs_mounted = false;
    return fs_from_block(operation, block, bytes);
}

FsResult flush_entry_result(uint32_t index, FsOperation operation) {
    if (index >= zfs_super.entry_count) return fs_failure(operation, FsStatus::invalid_argument);
    const BlockResult read = zfs_read_sector(entry_sector(index), sector_buffer);
    if (!read.ok()) return zfs_block_failure(operation, read);
    memcpy(sector_buffer + entry_slot_offset(index), &zfs_entries[index], sizeof(ZfsEntry));
    const BlockResult write = zfs_write_sector(entry_sector(index), sector_buffer);
    if (!write.ok()) return zfs_block_failure(operation, write);
    return fs_success(operation);
}

bool flush_entry(uint32_t index) {
    return flush_entry_result(index, FsOperation::sync).ok();
}

FsResult replace_entry_result(uint32_t index, const ZfsEntry& replacement,
                              FsOperation operation) {
    if (index >= zfs_super.entry_count) return fs_failure(operation, FsStatus::invalid_argument);
    const ZfsEntry previous = zfs_entries[index];
    zfs_entries[index] = replacement;
    const FsResult result = flush_entry_result(index, operation);
    if (!result.ok()) zfs_entries[index] = previous;
    return result;
}

FsResult load_entries_result() {
    memset(zfs_entries, 0, sizeof(zfs_entries));
    uint8_t* destination = reinterpret_cast<uint8_t*>(zfs_entries);
    for (uint32_t sector = 0; sector < zfs_super.entry_sectors; ++sector) {
        const BlockResult read = zfs_read_sector(1U + sector, destination + sector * sector_size);
        if (!read.ok()) return zfs_block_failure(FsOperation::mount, read);
    }
    return fs_success(FsOperation::mount);
}

bool load_entries() { return load_entries_result().ok(); }

FsResult recover_transactions_result() {
    uint32_t recovered = 0U;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        if (!zfs_entries[i].used || zfs_entries[i].type != zfs_type_transaction) continue;
        const ZfsEntry empty{};
        const FsResult cleared = replace_entry_result(i, empty, FsOperation::recover);
        if (!cleared.ok()) return cleared;
        ++recovered;
    }
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        const ZfsEntry committed = zfs_entries[i];
        if (!committed.used || committed.type != zfs_type_file ||
            !(committed.flags & zfs_flag_committed)) {
            continue;
        }
        const uint32_t old = committed.reserved;
        if (old < zfs_super.entry_count && old != i && zfs_entries[old].used) {
            const ZfsEntry empty{};
            const FsResult cleared_old = replace_entry_result(old, empty, FsOperation::recover);
            if (!cleared_old.ok()) return cleared_old;
        }
        ZfsEntry finalized = committed;
        finalized.flags &= static_cast<uint16_t>(~zfs_flag_committed);
        finalized.reserved = 0U;
        const FsResult finalized_result = replace_entry_result(i, finalized, FsOperation::recover);
        if (!finalized_result.ok()) return finalized_result;
        ++recovered;
    }
    if (recovered) serial::line("ZENOVFS_INTERRUPTED_WRITE_RECOVERED");
    return fs_success(FsOperation::recover, recovered);
}

bool recover_transactions() { return recover_transactions_result().ok(); }

bool mount_layout_valid() {
    if (!zfs_super.total_sectors || !zfs_super.entry_count ||
        zfs_super.entry_count > zfs_max_entries || !zfs_super.slot_sectors) {
        return false;
    }
    const uint32_t expected_entry_sectors = (zfs_super.entry_count + 7U) / 8U;
    if (zfs_super.entry_sectors != expected_entry_sectors ||
        zfs_super.data_start < 1U + zfs_super.entry_sectors ||
        zfs_super.total_sectors > ata0.sectors) {
        return false;
    }
    const uint64_t data_end = static_cast<uint64_t>(zfs_super.data_start) +
        static_cast<uint64_t>(zfs_super.entry_count) * zfs_super.slot_sectors;
    return data_end <= zfs_super.total_sectors;
}

FsResult mount_result() {
    zfs_mounted = false;
    if (!ata0.present) return fs_failure(FsOperation::mount, FsStatus::not_mounted);
    const BlockResult super_read = zfs_read_sector(0U, reinterpret_cast<uint8_t*>(&zfs_super));
    if (!super_read.ok()) return zfs_block_failure(FsOperation::mount, super_read);
    const char expected[8] = {'Z','E','N','O','V','F','S','1'};
    if (!bytes_equal(zfs_super.magic, expected, 8U) || zfs_super.version != 1U ||
        !mount_layout_valid()) {
        return fs_failure(FsOperation::mount, FsStatus::metadata_corrupt);
    }
    const FsResult loaded = load_entries_result();
    if (!loaded.ok()) return loaded;
    file_buffer = static_cast<uint8_t*>(heap::allocate(max_file_bytes(), 16U));
    if (!file_buffer) return fs_failure(FsOperation::mount, FsStatus::no_space);
    const FsResult recovered = recover_transactions_result();
    if (!recovered.ok()) return recovered;
    zfs_mounted = true;
    string_copy(current_directory, "/", sizeof(current_directory));
    return fs_success(FsOperation::mount, recovered.bytes);
}

bool mount() { return mount_result().ok(); }

FsResult read_file_result(const char* input, uint8_t* output, uint32_t capacity,
                          uint32_t& actual_size) {
    actual_size = 0U;
    if (!zfs_mounted) return fs_failure(FsOperation::read, FsStatus::not_mounted);
    if (!input || !output) return fs_failure(FsOperation::read, FsStatus::invalid_argument);
    char path[48];
    if (!normalize_path(input, path)) return fs_failure(FsOperation::read, FsStatus::invalid_path);
    const int32_t found = find_entry(path);
    if (found < 0) return fs_failure(FsOperation::read, FsStatus::not_found);
    const ZfsEntry& entry = zfs_entries[found];
    if (entry.type != zfs_type_file) return fs_failure(FsOperation::read, FsStatus::wrong_type);
    if (entry.size > max_file_bytes()) return fs_failure(FsOperation::read, FsStatus::metadata_corrupt);
    if (entry.size > capacity) return fs_failure(FsOperation::read, FsStatus::buffer_too_small);

    uint32_t copied = 0U;
    for (uint32_t sector = 0; copied < entry.size; ++sector) {
        const BlockResult read = zfs_read_sector(
            data_sector(static_cast<uint32_t>(found)) + sector, sector_buffer);
        if (!read.ok()) {
            if (copied) memset(output, 0, copied);
            return zfs_block_failure(FsOperation::read, read, copied);
        }
        uint32_t chunk = entry.size - copied;
        if (chunk > sector_size) chunk = sector_size;
        memcpy(output + copied, sector_buffer, chunk);
        copied += chunk;
    }
    if (fnv1a(output, entry.size) != entry.checksum) {
        if (entry.size) memset(output, 0, entry.size);
        return fs_failure(FsOperation::read, FsStatus::checksum_mismatch);
    }
    actual_size = entry.size;
    return fs_success(FsOperation::read, entry.size);
}

bool read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    return read_file_result(input, output, capacity, actual_size).ok();
}

FsResult write_file_result(const char* input, const uint8_t* data, uint32_t size, bool append) {
    if (!zfs_mounted) return fs_failure(FsOperation::write, FsStatus::not_mounted);
    if (!input || (!data && size)) return fs_failure(FsOperation::write, FsStatus::invalid_argument);
    char path[48];
    if (!normalize_path(input, path) || string_equal(path, "/")) {
        return fs_failure(FsOperation::write, FsStatus::invalid_path);
    }
    if (!parent_exists(path)) return fs_failure(FsOperation::write, FsStatus::parent_missing);

    const int32_t old_index = find_entry(path);
    uint32_t existing = 0U;
    if (old_index >= 0) {
        if (zfs_entries[old_index].type != zfs_type_file) {
            return fs_failure(FsOperation::write, FsStatus::wrong_type);
        }
        existing = append ? zfs_entries[old_index].size : 0U;
    }
    if (existing > max_file_bytes() || size > max_file_bytes() - existing) {
        return fs_failure(FsOperation::write, FsStatus::no_space);
    }
    if (append && existing) {
        uint32_t loaded = 0U;
        const FsResult read = read_file_result(path, file_buffer, max_file_bytes(), loaded);
        if (!read.ok() || loaded != existing) {
            return fs_result(FsOperation::write, read.status, 0U, read.block);
        }
    }
    if (size && data != file_buffer + existing) memcpy(file_buffer + existing, data, size);
    const uint32_t total = existing + size;
    const int32_t staging_index = free_entry(
        old_index >= 0 ? static_cast<uint32_t>(old_index) : transaction_none);
    if (staging_index < 0) return fs_failure(FsOperation::write, FsStatus::no_space);

    uint32_t written = 0U;
    for (uint32_t sector = 0; written < total; ++sector) {
        memset(sector_buffer, 0, sizeof(sector_buffer));
        uint32_t chunk = total - written;
        if (chunk > sector_size) chunk = sector_size;
        memcpy(sector_buffer, file_buffer + written, chunk);
        const BlockResult write = zfs_write_sector(
            data_sector(static_cast<uint32_t>(staging_index)) + sector, sector_buffer);
        if (!write.ok()) return zfs_block_failure(FsOperation::write, write, written);
        written += chunk;
    }

    ZfsEntry stage{};
    stage.used = 1U;
    stage.type = zfs_type_transaction;
    stage.size = total;
    stage.checksum = fnv1a(file_buffer, total);
    stage.reserved = old_index >= 0 ? static_cast<uint32_t>(old_index) : transaction_none;
    if (!path_copy(stage.path, path, sizeof(stage.path))) {
        return fs_failure(FsOperation::write, FsStatus::invalid_path);
    }
    FsResult metadata = replace_entry_result(static_cast<uint32_t>(staging_index), stage,
                                             FsOperation::write);
    if (!metadata.ok()) return metadata;

    stage.type = zfs_type_file;
    stage.flags = zfs_flag_committed;
    metadata = replace_entry_result(static_cast<uint32_t>(staging_index), stage,
                                    FsOperation::write);
    if (!metadata.ok()) return metadata;

    if (old_index >= 0) {
        const ZfsEntry empty{};
        const FsResult cleared_old = replace_entry_result(static_cast<uint32_t>(old_index), empty,
                                                          FsOperation::write);
        if (!cleared_old.ok()) {
            return fs_recovery_pending(FsOperation::write, total, cleared_old.block);
        }
    }

    stage.flags = 0U;
    stage.reserved = 0U;
    const FsResult finalized = replace_entry_result(static_cast<uint32_t>(staging_index), stage,
                                                    FsOperation::write);
    if (!finalized.ok()) {
        return fs_recovery_pending(FsOperation::write, total, finalized.block);
    }
    return fs_success(FsOperation::write, total);
}

bool write_file(const char* input, const uint8_t* data, uint32_t size, bool append) {
    return write_file_result(input, data, size, append).ok();
}

FsResult make_directory_result(const char* input) {
    if (!zfs_mounted) return fs_failure(FsOperation::create, FsStatus::not_mounted);
    char path[48];
    if (!normalize_path(input, path) || string_equal(path, "/")) {
        return fs_failure(FsOperation::create, FsStatus::invalid_path);
    }
    if (find_entry(path) >= 0) return fs_failure(FsOperation::create, FsStatus::already_exists);
    if (!parent_exists(path)) return fs_failure(FsOperation::create, FsStatus::parent_missing);
    const int32_t index = free_entry();
    if (index < 0) return fs_failure(FsOperation::create, FsStatus::no_space);
    ZfsEntry entry{};
    entry.used = 1U;
    entry.type = zfs_type_directory;
    if (!path_copy(entry.path, path, sizeof(entry.path))) {
        return fs_failure(FsOperation::create, FsStatus::invalid_path);
    }
    return replace_entry_result(static_cast<uint32_t>(index), entry, FsOperation::create);
}

bool make_directory(const char* input) { return make_directory_result(input).ok(); }

FsResult touch_result(const char* input) {
    char path[48];
    if (!normalize_path(input, path)) return fs_failure(FsOperation::create, FsStatus::invalid_path);
    const int32_t found = find_entry(path);
    if (found >= 0) {
        return zfs_entries[found].type == zfs_type_file
            ? fs_success(FsOperation::create)
            : fs_failure(FsOperation::create, FsStatus::wrong_type);
    }
    const FsResult written = write_file_result(path, nullptr, 0U, false);
    return fs_result(FsOperation::create, written.status, written.bytes, written.block);
}

bool touch(const char* input) { return touch_result(input).ok(); }

bool has_children(const char* path) {
    const uint32_t length = static_cast<uint32_t>(string_length(path));
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        if (zfs_entries[i].used && zfs_entries[i].type != zfs_type_transaction &&
            string_starts_with(zfs_entries[i].path, path) &&
            zfs_entries[i].path[length] == '/' && zfs_entries[i].path[length + 1U]) {
            return true;
        }
    }
    return false;
}

FsResult remove_result(const char* input) {
    if (!zfs_mounted) return fs_failure(FsOperation::remove, FsStatus::not_mounted);
    char path[48];
    if (!normalize_path(input, path) || string_equal(path, "/")) {
        return fs_failure(FsOperation::remove, FsStatus::invalid_path);
    }
    const int32_t index = find_entry(path);
    if (index < 0) return fs_failure(FsOperation::remove, FsStatus::not_found);
    if (zfs_entries[index].type == zfs_type_directory && has_children(path)) {
        return fs_failure(FsOperation::remove, FsStatus::directory_not_empty);
    }
    const ZfsEntry empty{};
    return replace_entry_result(static_cast<uint32_t>(index), empty, FsOperation::remove);
}

bool remove(const char* input) { return remove_result(input).ok(); }

FsResult change_directory_result(const char* input) {
    if (!zfs_mounted) return fs_failure(FsOperation::query, FsStatus::not_mounted);
    char path[48];
    if (!normalize_path(input, path)) return fs_failure(FsOperation::query, FsStatus::invalid_path);
    if (!string_equal(path, "/")) {
        const int32_t index = find_entry(path);
        if (index < 0) return fs_failure(FsOperation::query, FsStatus::not_found);
        if (zfs_entries[index].type != zfs_type_directory) {
            return fs_failure(FsOperation::query, FsStatus::wrong_type);
        }
    }
    return path_copy(current_directory, path, sizeof(current_directory))
        ? fs_success(FsOperation::query)
        : fs_failure(FsOperation::query, FsStatus::invalid_path);
}

bool change_directory(const char* input) { return change_directory_result(input).ok(); }

FsResult rename_entry_result(const char* source_input, const char* destination_input) {
    if (!zfs_mounted) return fs_failure(FsOperation::rename, FsStatus::not_mounted);
    char source[48], destination[48];
    if (!normalize_path(source_input, source) || !normalize_path(destination_input, destination) ||
        string_equal(source, "/")) {
        return fs_failure(FsOperation::rename, FsStatus::invalid_path);
    }
    if (find_entry(destination) >= 0) {
        return fs_failure(FsOperation::rename, FsStatus::already_exists);
    }
    if (!parent_exists(destination)) return fs_failure(FsOperation::rename, FsStatus::parent_missing);
    const int32_t index = find_entry(source);
    if (index < 0) return fs_failure(FsOperation::rename, FsStatus::not_found);
    if (zfs_entries[index].type == zfs_type_directory && has_children(source)) {
        return fs_failure(FsOperation::rename, FsStatus::directory_not_empty);
    }
    ZfsEntry renamed = zfs_entries[index];
    if (!path_copy(renamed.path, destination, sizeof(renamed.path))) {
        return fs_failure(FsOperation::rename, FsStatus::invalid_path);
    }
    return replace_entry_result(static_cast<uint32_t>(index), renamed, FsOperation::rename);
}

bool rename_entry(const char* source_input, const char* destination_input) {
    return rename_entry_result(source_input, destination_input).ok();
}

FsResult copy_file_result(const char* source, const char* destination) {
    if (!file_buffer) return fs_failure(FsOperation::copy, FsStatus::not_mounted);
    uint32_t size = 0U;
    const FsResult read = read_file_result(source, file_buffer, max_file_bytes(), size);
    if (!read.ok()) return fs_result(FsOperation::copy, read.status, 0U, read.block);
    const FsResult write = write_file_result(destination, file_buffer, size, false);
    return fs_result(FsOperation::copy, write.status, write.bytes, write.block);
}

bool copy_file(const char* source, const char* destination) {
    return copy_file_result(source, destination).ok();
}
