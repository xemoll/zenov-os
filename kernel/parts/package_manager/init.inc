void init() {
    initialized = false;
    if (!storage::ready() || !package_catalog::ready) { serial::line("ZENPKG_TRUST_OR_STORAGE_UNAVAILABLE"); return; }
    package_buffer = static_cast<uint8_t*>(heap::allocate(storage::file_capacity(), 16U));
    verification_buffer = static_cast<uint8_t*>(heap::allocate(storage::file_capacity(), 16U));
    if (!package_buffer || !verification_buffer) { serial::line("ZENPKG_BUFFER_ALLOCATION_FAILED"); return; }
    if (!ensure_directory("/packages") || !ensure_directory("/var") || !ensure_directory("/var/lib") || !ensure_directory("/var/lib/zenpkg")) {
        serial::line("ZENPKG_DIRECTORY_INIT_FAILED"); return;
    }
    storage::FileInfo state_info{};
    if (storage::query(state_path, state_info)) {
        if (state_info.type != storage::zfs_type_file || !load_database()) { serial::line("ZENPKG_DB_INVALID"); return; }
        serial::line("ZENPKG_DB_RECOVERED");
    } else {
        Database empty{}; initialize_empty_database(empty); database = empty;
        if (!storage::write_file(state_path, reinterpret_cast<const uint8_t*>(&empty), sizeof(empty), false) ||
            !storage::sync_metadata() || !load_database()) { serial::line("ZENPKG_DB_INIT_FAILED"); return; }
        serial::line("ZENPKG_DB_CREATED");
    }
    initialized = true;
    serial::line("ZENPKG_MANAGER_READY");
}
