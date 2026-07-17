namespace storage {
constexpr uint32_t sector_size = 512U;
constexpr uint16_t ata_data = 0x1F0, ata_sector_count = 0x1F2, ata_lba_low = 0x1F3, ata_lba_mid = 0x1F4, ata_lba_high = 0x1F5;
constexpr uint16_t ata_drive = 0x1F6, ata_status = 0x1F7, ata_command = 0x1F7, ata_control = 0x3F6;
constexpr uint8_t ata_busy = 0x80, ata_drq = 0x08, ata_error_bit = 0x01;
inline uint16_t in16(uint16_t port) { uint16_t value; asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }
inline void out16(uint16_t port, uint16_t value) { asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
bool wait_not_busy() { for (uint32_t timeout = 0; timeout < 1000000; ++timeout) if ((in8(ata_status) & ata_busy) == 0) return true; return false; }
bool wait_drq() { for (uint32_t timeout = 0; timeout < 1000000; ++timeout) { const uint8_t status = in8(ata_status); if (status & ata_error_bit) return false; if ((status & ata_busy) == 0 && (status & ata_drq)) return true; } return false; }
struct BlockDevice { const char* name; bool present, writable; uint32_t sectors; bool (*read_sector)(uint32_t, uint8_t*); bool (*write_sector)(uint32_t, const uint8_t*); };
bool ata_present = false; uint32_t ata_sector_total = 0;
void ata_select(uint32_t lba) { out8(ata_drive, static_cast<uint8_t>(0xE0U | ((lba >> 24U) & 0x0FU))); io_wait(); }
bool ata_identify() {
    out8(ata_control, 0x02); out8(ata_drive, 0xE0); io_wait(); out8(ata_sector_count, 0); out8(ata_lba_low, 0); out8(ata_lba_mid, 0); out8(ata_lba_high, 0); out8(ata_command, 0xEC);
    const uint8_t initial = in8(ata_status); if (initial == 0 || initial == 0xFF || !wait_not_busy() || in8(ata_lba_mid) || in8(ata_lba_high) || !wait_drq()) return false;
    uint16_t identify[256]; for (uint32_t i = 0; i < 256; ++i) identify[i] = in16(ata_data);
    ata_sector_total = static_cast<uint32_t>(identify[60]) | (static_cast<uint32_t>(identify[61]) << 16U); ata_present = ata_sector_total > 0; return ata_present;
}
bool ata_read_sector(uint32_t lba, uint8_t* output) {
    if (!ata_present || !output || lba >= ata_sector_total || !wait_not_busy()) return false; ata_select(lba);
    out8(ata_sector_count, 1); out8(ata_lba_low, static_cast<uint8_t>(lba)); out8(ata_lba_mid, static_cast<uint8_t>(lba >> 8U)); out8(ata_lba_high, static_cast<uint8_t>(lba >> 16U)); out8(ata_command, 0x20);
    if (!wait_drq()) return false; for (uint32_t i = 0; i < 256; ++i) { const uint16_t value = in16(ata_data); output[i * 2] = static_cast<uint8_t>(value); output[i * 2 + 1] = static_cast<uint8_t>(value >> 8U); } return true;
}
bool ata_write_sector(uint32_t lba, const uint8_t* input) {
    if (!ata_present || !input || lba >= ata_sector_total || !wait_not_busy()) return false; ata_select(lba);
    out8(ata_sector_count, 1); out8(ata_lba_low, static_cast<uint8_t>(lba)); out8(ata_lba_mid, static_cast<uint8_t>(lba >> 8U)); out8(ata_lba_high, static_cast<uint8_t>(lba >> 16U)); out8(ata_command, 0x30);
    if (!wait_drq()) return false; for (uint32_t i = 0; i < 256; ++i) out16(ata_data, static_cast<uint16_t>(input[i * 2]) | static_cast<uint16_t>(input[i * 2 + 1] << 8U));
    out8(ata_command, 0xE7); return wait_not_busy() && (in8(ata_status) & ata_error_bit) == 0;
}
BlockDevice ata0{"ata0", false, true, 0, ata_read_sector, ata_write_sector};

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
    if (!input || !*input || !output) return false; char combined[96]{}; const char* source = input;
    if (string_starts_with(source, "/data")) { source += 5; if (!*source) source = "/"; else if (*source != '/') return false; if (!path_copy(combined, source, sizeof(combined))) return false; }
    else if (*source == '/') { if (!path_copy(combined, source, sizeof(combined))) return false; }
    else { if (!path_copy(combined, current_directory, sizeof(combined))) return false; if (!string_equal(combined, "/") && !path_append(combined, "/", sizeof(combined))) return false; if (!path_append(combined, source, sizeof(combined))) return false; }
    output[0] = '/'; output[1] = 0; uint8_t starts[16]; uint32_t count = 0, position = 0;
    while (combined[position]) {
        while (combined[position] == '/') ++position; if (!combined[position]) break; char component[48]; uint32_t length = 0;
        while (combined[position] && combined[position] != '/') { if (length + 1 >= sizeof(component)) return false; component[length++] = combined[position++]; } component[length] = 0;
        if (string_equal(component, ".")) continue;
        if (string_equal(component, "..")) { if (count) { const uint32_t start = starts[--count]; output[start == 1 ? 1 : start - 1] = 0; } continue; }
        const uint32_t current = static_cast<uint32_t>(string_length(output)); if (count >= sizeof(starts)) return false; starts[count++] = static_cast<uint8_t>(current == 1 ? 1 : current + 1);
        if (!string_equal(output, "/") && !path_append(output, "/", 48)) return false; if (!path_append(output, component, 48)) return false;
    }
    return true;
}
int32_t find_entry(const char* path) { for (uint32_t i = 0; i < zfs_super.entry_count && i < zfs_max_entries; ++i) if (zfs_entries[i].used && zfs_entries[i].type != zfs_type_transaction && string_equal(zfs_entries[i].path, path)) return static_cast<int32_t>(i); return -1; }
int32_t free_entry(uint32_t excluded = transaction_none) { for (uint32_t i = 0; i < zfs_super.entry_count && i < zfs_max_entries; ++i) if (i != excluded && !zfs_entries[i].used) return static_cast<int32_t>(i); return -1; }
bool parent_path(const char* path, char* parent) { if (string_equal(path, "/") || !path_copy(parent, path, 48)) return false; uint32_t length = static_cast<uint32_t>(string_length(parent)); while (length > 1 && parent[length - 1] != '/') --length; if (length <= 1) { parent[0] = '/'; parent[1] = 0; } else parent[length - 1] = 0; return true; }
bool parent_exists(const char* path) { char parent[48]; if (!parent_path(path, parent) || string_equal(parent, "/")) return true; const int32_t index = find_entry(parent); return index >= 0 && zfs_entries[index].type == zfs_type_directory; }
bool direct_child(const char* directory, const char* path, const char*& display) {
    if (string_equal(directory, "/")) { if (path[0] != '/' || !path[1]) return false; display = path + 1; }
    else { const uint32_t length = static_cast<uint32_t>(string_length(directory)); if (!string_starts_with(path, directory) || path[length] != '/') return false; display = path + length + 1; }
    if (!*display) return false; for (const char* cursor = display; *cursor; ++cursor) if (*cursor == '/') return false; return true;
}
uint32_t entry_sector(uint32_t index) { return 1U + index / 8U; }
uint32_t entry_slot_offset(uint32_t index) { return (index % 8U) * sizeof(ZfsEntry); }
uint32_t data_sector(uint32_t index) { return zfs_super.data_start + index * zfs_super.slot_sectors; }
uint32_t max_file_bytes() { return zfs_super.slot_sectors * sector_size; }
bool flush_entry(uint32_t index) { if (index >= zfs_super.entry_count || !ata0.read_sector(entry_sector(index), sector_buffer)) return false; memcpy(sector_buffer + entry_slot_offset(index), &zfs_entries[index], sizeof(ZfsEntry)); return ata0.write_sector(entry_sector(index), sector_buffer); }
bool load_entries() { uint8_t* destination = reinterpret_cast<uint8_t*>(zfs_entries); for (uint32_t sector = 0; sector < zfs_super.entry_sectors; ++sector) if (!ata0.read_sector(1U + sector, destination + sector * sector_size)) return false; return true; }
bool recover_transactions() {
    bool recovered = false;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) if (zfs_entries[i].used && zfs_entries[i].type == zfs_type_transaction) { memset(&zfs_entries[i], 0, sizeof(ZfsEntry)); if (!flush_entry(i)) return false; recovered = true; }
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        ZfsEntry& committed = zfs_entries[i]; if (!committed.used || committed.type != zfs_type_file || !(committed.flags & zfs_flag_committed)) continue;
        const uint32_t old = committed.reserved;
        if (old < zfs_super.entry_count && old != i && zfs_entries[old].used) { memset(&zfs_entries[old], 0, sizeof(ZfsEntry)); if (!flush_entry(old)) return false; }
        committed.flags &= static_cast<uint16_t>(~zfs_flag_committed); committed.reserved = 0; if (!flush_entry(i)) return false; recovered = true;
    }
    if (recovered) serial::line("ZENOVFS_INTERRUPTED_WRITE_RECOVERED"); return true;
}
bool mount() {
    if (!ata0.present || !ata0.read_sector(0, reinterpret_cast<uint8_t*>(&zfs_super))) return false; const char expected[8] = {'Z','E','N','O','V','F','S','1'};
    if (!bytes_equal(zfs_super.magic, expected, 8) || zfs_super.version != 1 || !zfs_super.entry_count || zfs_super.entry_count > zfs_max_entries || zfs_super.total_sectors > ata0.sectors || !zfs_super.slot_sectors) return false;
    if (!load_entries()) return false; file_buffer = static_cast<uint8_t*>(heap::allocate(max_file_bytes(), 16)); if (!file_buffer || !recover_transactions()) return false;
    zfs_mounted = true; string_copy(current_directory, "/", sizeof(current_directory)); return true;
}

bool read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    actual_size = 0; if (!zfs_mounted || !output) return false; char path[48]; if (!normalize_path(input, path)) return false; const int32_t found = find_entry(path);
    if (found < 0 || zfs_entries[found].type != zfs_type_file || zfs_entries[found].size > capacity) return false; const uint32_t size = zfs_entries[found].size; uint32_t copied = 0;
    for (uint32_t sector = 0; copied < size; ++sector) { if (!ata0.read_sector(data_sector(static_cast<uint32_t>(found)) + sector, sector_buffer)) return false; uint32_t chunk = size - copied; if (chunk > sector_size) chunk = sector_size; memcpy(output + copied, sector_buffer, chunk); copied += chunk; }
    if (fnv1a(output, size) != zfs_entries[found].checksum) return false; actual_size = size; return true;
}
bool write_file(const char* input, const uint8_t* data, uint32_t size, bool append) {
    if (!zfs_mounted || (!data && size)) return false; char path[48]; if (!normalize_path(input, path) || string_equal(path, "/") || !parent_exists(path)) return false;
    const int32_t old_index = find_entry(path); uint32_t existing = 0;
    if (old_index >= 0) { if (zfs_entries[old_index].type != zfs_type_file) return false; existing = append ? zfs_entries[old_index].size : 0; }
    if (existing > max_file_bytes() || size > max_file_bytes() - existing) return false;
    if (append && existing) { uint32_t loaded = 0; if (!read_file(path, file_buffer, max_file_bytes(), loaded) || loaded != existing) return false; }
    if (size && data != file_buffer + existing) memcpy(file_buffer + existing, data, size); const uint32_t total = existing + size;
    const int32_t staging_index = free_entry(old_index >= 0 ? static_cast<uint32_t>(old_index) : transaction_none); if (staging_index < 0) return false;
    uint32_t written = 0; for (uint32_t sector = 0; written < total; ++sector) { memset(sector_buffer, 0, sizeof(sector_buffer)); uint32_t chunk = total - written; if (chunk > sector_size) chunk = sector_size; memcpy(sector_buffer, file_buffer + written, chunk); if (!ata0.write_sector(data_sector(static_cast<uint32_t>(staging_index)) + sector, sector_buffer)) return false; written += chunk; }
    ZfsEntry& stage = zfs_entries[staging_index]; memset(&stage, 0, sizeof(stage)); stage.used = 1; stage.type = zfs_type_transaction; stage.size = total; stage.checksum = fnv1a(file_buffer, total);
    stage.reserved = old_index >= 0 ? static_cast<uint32_t>(old_index) : transaction_none; if (!path_copy(stage.path, path, sizeof(stage.path)) || !flush_entry(static_cast<uint32_t>(staging_index))) return false;
    stage.type = zfs_type_file; stage.flags = zfs_flag_committed; if (!flush_entry(static_cast<uint32_t>(staging_index))) return false;
    if (old_index >= 0) { memset(&zfs_entries[old_index], 0, sizeof(ZfsEntry)); if (!flush_entry(static_cast<uint32_t>(old_index))) return true; }
    stage.flags = 0; stage.reserved = 0; flush_entry(static_cast<uint32_t>(staging_index)); return true;
}
bool make_directory(const char* input) { if (!zfs_mounted) return false; char path[48]; if (!normalize_path(input, path) || string_equal(path, "/") || find_entry(path) >= 0 || !parent_exists(path)) return false; const int32_t index = free_entry(); if (index < 0) return false; ZfsEntry& entry = zfs_entries[index]; memset(&entry, 0, sizeof(entry)); entry.used = 1; entry.type = zfs_type_directory; return path_copy(entry.path, path, sizeof(entry.path)) && flush_entry(static_cast<uint32_t>(index)); }
bool touch(const char* input) { char path[48]; if (!normalize_path(input, path)) return false; const int32_t found = find_entry(path); return found >= 0 ? zfs_entries[found].type == zfs_type_file : write_file(path, nullptr, 0, false); }
bool has_children(const char* path) { const uint32_t length = static_cast<uint32_t>(string_length(path)); for (uint32_t i = 0; i < zfs_super.entry_count; ++i) if (zfs_entries[i].used && zfs_entries[i].type != zfs_type_transaction && string_starts_with(zfs_entries[i].path, path) && zfs_entries[i].path[length] == '/' && zfs_entries[i].path[length + 1]) return true; return false; }
bool remove(const char* input) { if (!zfs_mounted) return false; char path[48]; if (!normalize_path(input, path) || string_equal(path, "/")) return false; const int32_t index = find_entry(path); if (index < 0 || (zfs_entries[index].type == zfs_type_directory && has_children(path))) return false; memset(&zfs_entries[index], 0, sizeof(ZfsEntry)); return flush_entry(static_cast<uint32_t>(index)); }
bool change_directory(const char* input) { if (!zfs_mounted) return false; char path[48]; if (!normalize_path(input, path)) return false; if (!string_equal(path, "/")) { const int32_t index = find_entry(path); if (index < 0 || zfs_entries[index].type != zfs_type_directory) return false; } return path_copy(current_directory, path, sizeof(current_directory)); }
bool rename_entry(const char* source_input, const char* destination_input) { char source[48], destination[48]; if (!normalize_path(source_input, source) || !normalize_path(destination_input, destination) || string_equal(source, "/") || find_entry(destination) >= 0 || !parent_exists(destination)) return false; const int32_t index = find_entry(source); if (index < 0 || (zfs_entries[index].type == zfs_type_directory && has_children(source))) return false; return path_copy(zfs_entries[index].path, destination, sizeof(zfs_entries[index].path)) && flush_entry(static_cast<uint32_t>(index)); }
bool copy_file(const char* source, const char* destination) { uint32_t size = 0; return read_file(source, file_buffer, max_file_bytes(), size) && write_file(destination, file_buffer, size, false); }

void list(const char* input) {
    char directory[48]; if (!input || !*input) path_copy(directory, current_directory, sizeof(directory)); else if (!normalize_path(input, directory)) { console::warning("Invalid path."); return; }
    if (!string_equal(directory, "/")) { const int32_t index = find_entry(directory); if (index < 0 || zfs_entries[index].type != zfs_type_directory) { console::warning("Directory not found."); return; } }
    console::line("  NAME                              TYPE       SIZE"); console::line("  --------------------------------- ---------- ------"); uint32_t shown = 0;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) { if (!zfs_entries[i].used || zfs_entries[i].type == zfs_type_transaction) continue; const char* display = nullptr; if (!direct_child(directory, zfs_entries[i].path, display)) continue;
        console::write("  "); console::write(display); const uint32_t length = static_cast<uint32_t>(string_length(display)); for (uint32_t p = length; p < 34; ++p) console::put(' ');
        if (zfs_entries[i].type == zfs_type_directory) console::line("directory"); else { console::write("file       "); console::unsigned_dec(zfs_entries[i].size); console::line(" B"); } ++shown; }
    if (!shown) console::line("  <empty>");
}
void cat(const char* input) { uint32_t size = 0; if (!read_file(input, file_buffer, max_file_bytes(), size)) { console::warning("File not found or checksum failed."); return; } for (uint32_t i = 0; i < size; ++i) console::put(static_cast<char>(file_buffer[i])); if (!size || file_buffer[size - 1] != '\n') console::put('\n'); }
void stat(const char* input) { char path[48]; if (!normalize_path(input, path)) { console::warning("Invalid path."); return; } if (string_equal(path, "/")) { console::line("  Path          /data"); console::line("  Type          directory"); return; } const int32_t index = find_entry(path); if (index < 0) { console::warning("Path not found."); return; } console::write("  Path          /data"); console::line(path); console::write("  Type          "); console::line(zfs_entries[index].type == zfs_type_directory ? "directory" : "file"); if (zfs_entries[index].type == zfs_type_file) { console::write("  Size          "); console::unsigned_dec(zfs_entries[index].size); console::line(" bytes"); console::write("  Checksum      "); console::hex(zfs_entries[index].checksum); console::put('\n'); } }
void print_working_directory() { console::write("/data"); if (!string_equal(current_directory, "/")) console::write(current_directory); console::put('\n'); }
void print_mounts() { console::line("  SOURCE        TARGET   FILESYSTEM  ACCESS"); console::line("  kernel.zv     /system  initramfs   read-only"); console::line(zfs_mounted ? "  /dev/ata0     /data    ZenovFS1    transactional" : "  /dev/ata0     /data    unavailable -"); }
void print_df() { if (!zfs_mounted) { console::warning("Persistent volume is not mounted."); return; } uint32_t files = 0, directories = 0, bytes = 0; for (uint32_t i = 0; i < zfs_super.entry_count; ++i) { if (!zfs_entries[i].used || zfs_entries[i].type == zfs_type_transaction) continue; if (zfs_entries[i].type == zfs_type_directory) ++directories; else { ++files; bytes += zfs_entries[i].size; } } console::write("  Files         "); console::unsigned_dec(files); console::put('\n'); console::write("  Directories   "); console::unsigned_dec(directories); console::put('\n'); console::write("  Payload       "); console::unsigned_dec(bytes); console::line(" bytes"); }
bool ready() { return zfs_mounted; }
uint32_t file_capacity() { return zfs_mounted ? max_file_bytes() : 0; }
void init() { heap::init(); serial::line("Storage: probing ATA primary master..."); ata0.present = ata_identify(); ata0.sectors = ata_sector_total; if (!ata0.present) { serial::line("Storage: ATA device not found"); return; } serial::line("Storage: ATA PIO device online"); if (!mount()) { serial::line("Storage: ZenovFS mount failed"); return; } serial::line("ZENOVFS_MOUNT_OK"); }
} // namespace storage
