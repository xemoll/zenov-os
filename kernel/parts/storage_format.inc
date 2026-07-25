constexpr uint32_t zfs_format_max_slot_sectors = (64U * 1024U) / sector_size;
constexpr uint32_t zfs_format_lba28_sector_limit = 0x10000000U;

bool zfs_format_zero(const uint8_t* bytes, uint32_t size) {
    if (!bytes) return false;
    for (uint32_t i = 0; i < size; ++i) if (bytes[i] != 0U) return false;
    return true;
}

uint32_t zfs_format_path_length(const char path[48]) {
    uint32_t length = 0U;
    while (length < 48U && path[length]) ++length;
    return length;
}

bool zfs_format_path_equal(const char left[48], const char right[48]) {
    const uint32_t left_length = zfs_format_path_length(left);
    const uint32_t right_length = zfs_format_path_length(right);
    if (left_length >= 48U || left_length != right_length) return false;
    for (uint32_t i = 0; i < left_length; ++i) if (left[i] != right[i]) return false;
    return true;
}

bool zfs_format_dot_component(const char* path, uint32_t start, uint32_t end) {
    const uint32_t length = end - start;
    return (length == 1U && path[start] == '.') || (length == 2U && path[start] == '.' && path[start + 1U] == '.');
}

bool zfs_format_path_valid(const char path[48]) {
    const uint32_t length = zfs_format_path_length(path);
    if (length < 2U || length >= 48U || path[0] != '/' || path[length - 1U] == '/') return false;
    for (uint32_t i = length + 1U; i < 48U; ++i) if (path[i] != 0) return false;
    uint32_t component = 1U, components = 1U;
    for (uint32_t i = 1U; i < length; ++i) {
        const uint8_t value = static_cast<uint8_t>(path[i]);
        if (value < 0x20U || value == 0x7FU) return false;
        if (path[i] != '/') continue;
        if (i == component || zfs_format_dot_component(path, component, i)) return false;
        if (++components > 16U) return false;
        component = i + 1U;
    }
    return component < length && !zfs_format_dot_component(path, component, length);}

bool zfs_format_label_valid(const char label[16]) {
    uint32_t length = 0U;
    while (length < 16U && label[length]) {
        const uint8_t value = static_cast<uint8_t>(label[length]);
        if (value < 0x20U || value == 0x7FU) return false;
        ++length;
    }
    if (!length || length >= 16U) return false;
    for (uint32_t i = length + 1U; i < 16U; ++i) if (label[i] != 0) return false;
    return true;
}

bool zfs_format_superblock_valid(const ZfsSuperblock& super, uint32_t device_sectors, const char*& reason) {
    const char magic[8] = {'Z','E','N','O','V','F','S','1'};
    for (uint32_t i = 0; i < 8U; ++i) if (super.magic[i] != magic[i]) { reason = "magic"; return false; }
    if (super.version != 1U) { reason = "version"; return false; }
    if (!device_sectors || !super.total_sectors || super.total_sectors > device_sectors || super.total_sectors > zfs_format_lba28_sector_limit) { reason = "device-size"; return false; }
    if (!super.entry_count || super.entry_count > zfs_max_entries) { reason = "entry-count"; return false; }
    const uint32_t expected_entry_sectors = (super.entry_count * static_cast<uint32_t>(sizeof(ZfsEntry)) + sector_size - 1U) / sector_size;
    if (super.entry_sectors != expected_entry_sectors || super.entry_sectors > sizeof(ZfsEntry) * zfs_max_entries / sector_size) { reason = "entry-sectors"; return false; }
    const uint32_t metadata_end = 1U + super.entry_sectors;
    if (super.data_start < metadata_end || super.data_start >= super.total_sectors) { reason = "metadata-layout"; return false; }
    if (!super.slot_sectors || super.slot_sectors > zfs_format_max_slot_sectors) { reason = "slot-size"; return false; }
    if (super.entry_count > (super.total_sectors - super.data_start) / super.slot_sectors) { reason = "data-layout"; return false; }
    if (!super.generation) { reason = "generation"; return false; }
    if (!zfs_format_label_valid(super.label)) { reason = "label"; return false; }
    if (!zfs_format_zero(super.reserved, sizeof(super.reserved))) { reason = "superblock-reserved"; return false; }
    reason = nullptr;
    return true;
}

bool zfs_format_unused_entry_zero(const ZfsEntry& entry) {
    return zfs_format_zero(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));
}

bool zfs_format_parent_present(const ZfsSuperblock& super, const ZfsEntry* entries, const char path[48]) {
    const uint32_t length = zfs_format_path_length(path);
    uint32_t slash = length;
    while (slash > 0U && path[slash - 1U] != '/') --slash;
    if (slash <= 1U) return true;
    const uint32_t parent_length = slash - 1U;
    for (uint32_t i = 0; i < super.entry_count; ++i) {
        const ZfsEntry& candidate = entries[i];
        if (!candidate.used || candidate.type != zfs_type_directory || zfs_format_path_length(candidate.path) != parent_length) continue;
        bool equal = true;
        for (uint32_t p = 0; p < parent_length; ++p) if (candidate.path[p] != path[p]) { equal = false; break; }
        if (equal) return true;
    }
    return false;
}

bool zfs_format_transition(const ZfsEntry& entry) {
    return entry.type == zfs_type_transaction || (entry.type == zfs_type_file && (entry.flags & zfs_flag_committed));
}

bool zfs_format_entries_valid(const ZfsSuperblock& super, const ZfsEntry* entries, const char*& reason) {
    if (!entries) { reason = "entry-pointer"; return false; }
    const uint32_t maximum_file_bytes = super.slot_sectors * sector_size;
    for (uint32_t i = 0; i < super.entry_count; ++i) {
        const ZfsEntry& entry = entries[i];
        if (!entry.used) {
            if (!zfs_format_unused_entry_zero(entry)) { reason = "unused-entry-data"; return false; }
            continue;
        }
        if (entry.used != 1U) { reason = "entry-used"; return false; }
        if (entry.type < zfs_type_file || entry.type > zfs_type_transaction) { reason = "entry-type"; return false; }
        if (!zfs_format_path_valid(entry.path)) { reason = "entry-path"; return false; }
        if (entry.flags & static_cast<uint16_t>(~zfs_flag_committed)) { reason = "entry-flags"; return false; }
        if (entry.type == zfs_type_directory) {
            if (entry.flags || entry.size || entry.checksum || entry.reserved) { reason = "directory-metadata"; return false; }
        } else {
            if (entry.size > maximum_file_bytes) { reason = "file-size"; return false; }
            if (entry.type == zfs_type_transaction && entry.flags) { reason = "transaction-flags"; return false; }
            if (entry.type == zfs_type_file && !(entry.flags & zfs_flag_committed) && entry.reserved) { reason = "file-reserved"; return false; }
            if (zfs_format_transition(entry) && entry.reserved != transaction_none && (entry.reserved >= super.entry_count || entry.reserved == i)) { reason = "transition-reference"; return false; }
        }
        if (!zfs_format_parent_present(super, entries, entry.path)) { reason = "missing-parent"; return false; }
    }
    for (uint32_t i = 0; i < super.entry_count; ++i) {
        const ZfsEntry& entry = entries[i];
        if (!entry.used || !zfs_format_transition(entry) || entry.reserved == transaction_none) continue;
        const ZfsEntry& previous = entries[entry.reserved];
        if (entry.type == zfs_type_transaction && !previous.used) { reason = "transaction-old-missing"; return false; }
        if (previous.used && (previous.type != zfs_type_file || previous.flags != 0U || !zfs_format_path_equal(entry.path, previous.path))) { reason = "transition-old-mismatch"; return false; }
        for (uint32_t j = i + 1U; j < super.entry_count; ++j) {
            const ZfsEntry& other = entries[j];
            if (other.used && zfs_format_transition(other) && other.reserved == entry.reserved) { reason = "transition-reference-shared"; return false; }
        }
    }
    for (uint32_t i = 0; i < super.entry_count; ++i) {
        if (!entries[i].used) continue;
        for (uint32_t j = i + 1U; j < super.entry_count; ++j) {
            if (!entries[j].used || !zfs_format_path_equal(entries[i].path, entries[j].path)) continue;
            const bool allowed = (zfs_format_transition(entries[i]) && entries[i].reserved == j) || (zfs_format_transition(entries[j]) && entries[j].reserved == i);
            if (!allowed) { reason = "duplicate-path"; return false; }
        }
    }
    reason = nullptr;
    return true;
}
