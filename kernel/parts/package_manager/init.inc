void init() {
    initialized = false;
    if (!storage::ready()) {
        serial::line("ZENPKG_STORAGE_UNAVAILABLE");
        return;
    }
    if (!package_repository::ready) {
        serial::line("ZENPKG_REPOSITORY_UNAVAILABLE");
        return;
    }
    if (!crypto::sha256_self_test()) {
        serial::line("ZENPKG_SHA256_SELF_TEST_FAILED");
        return;
    }
    serial::line("ZENPKG_SHA256_OK");
    package_buffer = package_storage;
    memset(package_buffer, 0, maximum_package_bytes);
    if (storage::file_capacity() > maximum_package_bytes) {
        serial::line("ZENPKG_STORAGE_SLOT_LIMIT_UNSUPPORTED");
        package_buffer = nullptr;
        return;
    }
    if (!ensure_directory("/packages") || !ensure_directory("/var") || !ensure_directory("/var/lib") ||
        !ensure_directory("/var/lib/zenpkg") || !ensure_directory("/var/cache") || !ensure_directory(cache_directory)) {
        serial::line("ZENPKG_DIRECTORY_INIT_FAILED");
        return;
    }
    storage::FileInfo state_info{};
    if (storage::query(state_path, state_info)) {
        if (state_info.type != storage::zfs_type_file || !load_database()) {
            serial::line("ZENPKG_DB_INVALID");
            return;
        }
        serial::line("ZENPKG_DB_RECOVERED");
    } else {
        Database empty{};
        initialize_empty_database(empty);
        database = empty;
        if (!storage::package_write_file(state_path, reinterpret_cast<const uint8_t*>(&empty), sizeof(empty), false) ||
            !storage::sync_metadata() || !load_database()) {
            serial::line("ZENPKG_DB_INIT_FAILED");
            return;
        }
        serial::line("ZENPKG_DB_CREATED");
    }
    if (!active_payloads_valid()) {
        serial::line("ZENPKG_ACTIVE_PAYLOAD_INVALID");
        return;
    }
    if (!cache_verify()) {
        serial::line("ZENPKG_CACHE_INIT_REJECTED");
        return;
    }
    initialized = true;
    serial::write("ZENPKG_CATALOG_READY entries=");
    process::serial_u32(authorized_package_count());
    serial::write("\r\n");
    serial::line("ZENPKG_CACHE_READY");
    serial::line("ZENPKG_MANAGER_READY");
}
