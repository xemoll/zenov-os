namespace storage {
bool security_read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    if (!read_file(input, output, capacity, actual_size)) return false;
    if (output != process::application_buffer) return true;
    if (!zgdb::precheck(input, output, actual_size)) return false;
    if (!security_guard::allow_execution(input, output, actual_size)) return false;
    if (!process::activate_capability_profile(input, output, actual_size)) {
        serial::line("SYSCALL_CAPABILITY_ACTIVATION_FAILED");
        return false;
    }
    return true;
}
} // namespace storage
