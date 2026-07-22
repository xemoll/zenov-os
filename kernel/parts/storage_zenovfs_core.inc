struct [[gnu::packed]] ZfsSuperblock { char magic[8]; uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation; char label[16]; uint8_t reserved[460]; };
struct [[gnu::packed]] ZfsEntry { uint8_t used, type; uint16_t flags; char path[48]; uint32_t size, checksum, reserved; };
static_assert(sizeof(ZfsSuperblock) == sector_size && sizeof(ZfsEntry) == 64);
constexpr uint32_t zfs_max_entries = 128U, transaction_none = 0xFFFFFFFFU;
constexpr uint8_t zfs_type_file = 1, zfs_type_directory = 2, zfs_type_transaction = 3;
constexpr uint16_t zfs_flag_committed = 0x0001U;
ZfsSuperblock zfs_super{}; ZfsEntry zfs_entries[zfs_max_entries]{};
bool zfs_mounted = false; char current_directory[48] = "/"; uint8_t sector_buffer[sector_size]; uint8_t* file_buffer = nullptr;

bool bytes_equal(const char* left, const char* right, uint32_t count) { for (uint32_t i = 0; i < count; ++i) if (left[i] != right[i]) return false; return true; }
uint32_t fnv1a(const uint8_t* data, uint32_t size) { uint32_t hash = 2166136261U; for (uint32_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; } return hash; }
bool path_copy(char* output, const char* input, uint32_t capacity) { if (!output || !input || !capacity) return false; uint32_t n = 0; while (input[n]) { if (n + 1 >= capacity) return false; output[n] = input[n]; ++n; } output[n] = 0; return true; }
bool path_append(char* output, const char* input, uint32_t capacity) { uint32_t n = static_cast<uint32_t>(string_length(output)), i = 0; while (input[i]) { if (n + 1 >= capacity) return false; output[n++] = input[i++]; } output[n] = 0; return true; }
bool normalize_path(const char* input, char* output) {
    if (!input || !*input || !output) return false;
    char combined[96]{};
    const char* source = input;
    if (string_starts_with(source, "/data")) {
        source += 5;
        if (!*source) source = "/";
        else if (*source != '/') return false;
        if (!path_copy(combined, source, sizeof(combined))) return false;
    } else if (*source == '/') {
        if (!path_copy(combined, source, sizeof(combined))) return false;
    } else {
        if (!path_copy(combined, current_directory, sizeof(combined))) return false;
        if (!string_equal(combined, "/") && !path_append(combined, "/", sizeof(combined))) return false;
        if (!path_append(combined, source, sizeof(combined))) return false;
    }
    output[0] = '/'; output[1] = 0; uint8_t starts[16]; uint32_t count = 0, position = 0;
    while (combined[position]) {
        while (combined[position] == '/') ++position;
        if (!combined[position]) break;
        char component[48]; uint32_t length = 0;
        while (combined[position] && combined[position] != '/') {
            if (length + 1 >= sizeof(component)) return false;
            component[length++] = combined[position++];
        }
        component[length] = 0;
        if (string_equal(component, ".")) continue;
        if (string_equal(component, "..")) {
            if (count) { const uint32_t start = starts[--count]; output[start == 1 ? 1 : start - 1] = 0; }
            continue;
        }
        const uint32_t current = static_cast<uint32_t>(string_length(output));
        if (count >= sizeof(starts)) return false;
        starts[count++] = static_cast<uint8_t>(current == 1 ? 1 : current + 1);
        if (!string_equal(output, "/") && !path_append(output, "/", 48)) return false;
        if (!path_append(output, component, 48)) return false;
    }
    return true;
}
int32_t find_entry(const char* path) { for (uint32_t i = 0; i < zfs_super.entry_count && i < zfs_max_entries; ++i) if (zfs_entries[i].used && zfs_entries[i].type != zfs_type_transaction && string_equal(zfs_entries[i].path, path)) return static_cast<int32_t>(i); return -1; }
int32_t free_entry(uint32_t excluded = transaction_none) { for (uint32_t i = 0; i < zfs_super.entry_count && i < zfs_max_entries; ++i) if (i != excluded && !zfs_entries[i].used) return static_cast<int32_t>(i); return -1; }
bool parent_path(const char* path, char* parent) { if (string_equal(path, "/") || !path_copy(parent, path, 48)) return false; uint32_t length = static_cast<uint32_t>(string_length(parent)); while (length > 1 && parent[length - 1] != '/') --length; if (length <= 1) { parent[0] = '/'; parent[1] = 0; } else parent[length - 1] = 0; return true; }
bool parent_exists(const char* path) { char parent[48]; if (!parent_path(path, parent) || string_equal(parent, "/")) return true; const int32_t index = find_entry(parent); return index >= 0 && zfs_entries[index].type == zfs_type_directory; }
bool direct_child(const char* directory, const char* path, const char*& display) {
    if (string_equal(directory, "/")) {
        if (path[0] != '/' || !path[1]) return false;
        display = path + 1;
    } else {
        const uint32_t length = static_cast<uint32_t>(string_length(directory));
        if (!string_starts_with(path, directory) || path[length] != '/') return false;
        display = path + length + 1;
    }
    if (!*display) return false;
    for (const char* cursor = display; *cursor; ++cursor) if (*cursor == '/') return false;
    return true;
}
uint32_t entry_sector(uint32_t index) { return 1U + index / 8U; }
uint32_t entry_slot_offset(uint32_t index) { return (index % 8U) * sizeof(ZfsEntry); }
uint32_t data_sector(uint32_t index) { return zfs_super.data_start + index * zfs_super.slot_sectors; }
uint32_t max_file_bytes() { return zfs_super.slot_sectors * sector_size; }
