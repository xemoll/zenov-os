namespace storage {
struct BrowserEntry {
    char name[48];
    char path[48];
    uint8_t type;
    uint32_t size;
    uint32_t checksum;
};

bool browser_directory_exists(const char* input, char normalized[48]) {
    if (!zfs_mounted || !input || !normalized || !normalize_path(input, normalized)) return false;
    if (string_equal(normalized, "/")) return true;
    const int32_t index = find_entry(normalized);
    return index >= 0 && zfs_entries[index].type == zfs_type_directory;
}

bool browser_entry_less(const BrowserEntry& left, const BrowserEntry& right) {
    const bool left_directory = left.type == zfs_type_directory;
    const bool right_directory = right.type == zfs_type_directory;
    if (left_directory != right_directory) return left_directory;
    return string_compare(left.name, right.name) < 0;
}

uint32_t list_directory(const char* input, BrowserEntry* output, uint32_t capacity) {
    if (!output || capacity == 0U) return 0;
    char directory[48];
    if (!browser_directory_exists(input, directory)) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < zfs_super.entry_count && i < zfs_max_entries; ++i) {
        const ZfsEntry& entry = zfs_entries[i];
        if (!entry.used || entry.type == zfs_type_transaction) continue;
        const char* display = nullptr;
        if (!direct_child(directory, entry.path, display)) continue;
        if (count >= capacity) break;
        BrowserEntry& item = output[count++];
        string_copy(item.name, display, sizeof(item.name));
        string_copy(item.path, entry.path, sizeof(item.path));
        item.type = entry.type;
        item.size = entry.size;
        item.checksum = entry.checksum;
    }

    for (uint32_t i = 1; i < count; ++i) {
        BrowserEntry value = output[i];
        uint32_t position = i;
        while (position > 0U && browser_entry_less(value, output[position - 1U])) {
            output[position] = output[position - 1U];
            --position;
        }
        output[position] = value;
    }
    return count;
}

bool browser_parent(const char* input, char output[48]) {
    char normalized[48];
    if (!browser_directory_exists(input, normalized) || string_equal(normalized, "/")) return false;
    return parent_path(normalized, output);
}

bool read_file_preview(const char* input, uint8_t* output, uint32_t capacity,
                       uint32_t& copied, uint32_t& total_size) {
    copied = 0;
    total_size = 0;
    if (!zfs_mounted || !input || !output || capacity == 0U) return false;
    char path[48];
    if (!normalize_path(input, path)) return false;
    const int32_t found = find_entry(path);
    if (found < 0 || zfs_entries[found].type != zfs_type_file) return false;

    total_size = zfs_entries[found].size;
    const uint32_t wanted = total_size < capacity ? total_size : capacity;
    uint8_t block[sector_size];
    uint32_t sector = 0;
    while (copied < wanted) {
        if (!ata0.read_sector(data_sector(static_cast<uint32_t>(found)) + sector, block)) return false;
        uint32_t chunk = wanted - copied;
        if (chunk > sector_size) chunk = sector_size;
        memcpy(output + copied, block, chunk);
        copied += chunk;
        ++sector;
    }
    return true;
}

uint32_t browser_generation() { return zfs_mounted ? zfs_super.generation : 0U; }
bool browser_mounted() { return zfs_mounted; }
} // namespace storage
