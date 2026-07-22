namespace storage {
bool security_read_blocked = false;
bool security_read_was_blocked() { return security_read_blocked; }

bool guarded_read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    security_read_blocked = false;
    if (!read_file(input, output, capacity, actual_size)) return false;
    if (security_guard::allow_persistent_read(input, output, actual_size)) return true;
    security_read_blocked = true;
    if (output && actual_size) memset(output, 0, actual_size);
    actual_size = 0U;
    return false;
}

bool security_read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    security_read_blocked = false;
    if (output != process::application_buffer) return guarded_read_file(input, output, capacity, actual_size);
    if (!read_file(input, output, capacity, actual_size)) return false;
    if (!zgdb::precheck(input, output, actual_size)) { memset(output, 0, actual_size); actual_size = 0U; return false; }
    if (package_manager::allow_execution(input, output, actual_size)) {
        if (!package_manager::activate_capabilities(input, output, actual_size)) {
            serial::line("ZENPKG_SYSCALL_CAPABILITY_ACTIVATION_FAILED");
            memset(output, 0, actual_size); actual_size = 0U;
            return false;
        }
        return true;
    }
    if (!security_guard::allow_execution(input, output, actual_size)) { memset(output, 0, actual_size); actual_size = 0U; return false; }
    if (!process::activate_capability_profile(input, output, actual_size)) {
        serial::line("SYSCALL_CAPABILITY_ACTIVATION_FAILED");
        memset(output, 0, actual_size); actual_size = 0U;
        return false;
    }
    return true;
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
