namespace storage {
bool security_read_blocked = false;
bool security_read_was_blocked() { return security_read_blocked; }

FsResult guarded_read_file_result(const char* input, uint8_t* output, uint32_t capacity,
                                  uint32_t& actual_size) {
    security_read_blocked = false;
    const FsResult read = read_file_result(input, output, capacity, actual_size);
    if (!read.ok()) return read;

    const auto deny = [&]() {
        security_read_blocked = true;
        if (output && actual_size) memset(output, 0, actual_size);
        actual_size = 0U;
        return fs_failure(FsOperation::read, FsStatus::permission_denied);
    };

    if (!security_guard::write_scan_buffer ||
        actual_size > security_guard::write_scan_workspace_bytes) return deny();
    if (actual_size) memcpy(security_guard::write_scan_buffer, output, actual_size);
    if (!security_guard::allow_persistent_read(
            input, security_guard::write_scan_buffer, actual_size)) return deny();
    if (actual_size) memcpy(output, security_guard::write_scan_buffer, actual_size);
    return read;
}

bool guarded_read_file(const char* input, uint8_t* output, uint32_t capacity,
                       uint32_t& actual_size) {
    return fs_record_result(
        guarded_read_file_result(input, output, capacity, actual_size)).ok();
}

FsResult security_read_file_result(const char* input, uint8_t* output, uint32_t capacity,
                                   uint32_t& actual_size) {
    security_read_blocked = false;
    if (output != process::application_buffer) return guarded_read_file_result(input, output, capacity, actual_size);

    const FsResult read = read_file_result(input, output, capacity, actual_size);
    if (!read.ok()) return read;

    const auto deny = [&]() {
        if (output && actual_size) memset(output, 0, actual_size);
        actual_size = 0U;
        return fs_failure(FsOperation::read, FsStatus::permission_denied);
    };

    if (!zgdb::precheck(input, output, actual_size)) return deny();
    if (package_manager::allow_execution(input, output, actual_size)) {
        if (!package_manager::activate_capabilities(input, output, actual_size)) {
            serial::line("ZENPKG_SYSCALL_CAPABILITY_ACTIVATION_FAILED");
            return deny();
        }
        return read;
    }
    if (!security_guard::allow_execution(input, output, actual_size)) return deny();
    if (!process::activate_capability_profile(input, output, actual_size)) {
        serial::line("SYSCALL_CAPABILITY_ACTIVATION_FAILED");
        return deny();
    }
    return read;
}

bool security_read_file(const char* input, uint8_t* output, uint32_t capacity,
                        uint32_t& actual_size) {
    return fs_record_result(
        security_read_file_result(input, output, capacity, actual_size)).ok();
}

void guarded_cat(const char* input) {
    uint32_t size = 0U;
    if (!guarded_read_file(input, file_buffer, max_file_bytes(), size)) {
        console::warning("File unavailable or blocked by ZenovGuard on-access scanning.");
        return;
    }
    for (uint32_t i = 0U; i < size; ++i) console::put(static_cast<char>(file_buffer[i]));
    if (!size || file_buffer[size - 1U] != '\n') console::put('\n');
}
} // namespace storage
