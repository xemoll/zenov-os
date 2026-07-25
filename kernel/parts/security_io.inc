namespace storage {
FsResult security_read_file_result(const char* input, uint8_t* output, uint32_t capacity,
                                   uint32_t& actual_size) {
    const FsResult read = read_file_result(input, output, capacity, actual_size);
    if (!read.ok()) return read;
    if (output != process::application_buffer) return read;

    const auto deny = [&]() {
        if (actual_size) memset(output, 0, actual_size);
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
} // namespace storage
