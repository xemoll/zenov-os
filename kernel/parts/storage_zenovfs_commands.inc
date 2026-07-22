void list(const char* input) {
    char directory[48];
    if (!input || !*input) path_copy(directory, current_directory, sizeof(directory));
    else if (!normalize_path(input, directory)) { console::warning("Invalid path."); return; }
    if (!string_equal(directory, "/")) { const int32_t index = find_entry(directory); if (index < 0 || zfs_entries[index].type != zfs_type_directory) { console::warning("Directory not found."); return; } }
    console::line("  NAME                              TYPE       SIZE"); console::line("  --------------------------------- ---------- ------"); uint32_t shown = 0;
    for (uint32_t i = 0; i < zfs_super.entry_count; ++i) {
        if (!zfs_entries[i].used || zfs_entries[i].type == zfs_type_transaction) continue;
        const char* display = nullptr;
        if (!direct_child(directory, zfs_entries[i].path, display)) continue;
        console::write("  "); console::write(display); const uint32_t length = static_cast<uint32_t>(string_length(display));
        for (uint32_t p = length; p < 34; ++p) console::put(' ');
        if (zfs_entries[i].type == zfs_type_directory) console::line("directory");
        else { console::write("file       "); console::unsigned_dec(zfs_entries[i].size); console::line(" B"); }
        ++shown;
    }
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
