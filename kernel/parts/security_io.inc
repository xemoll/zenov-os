namespace storage {
bool security_read_file(const char* input, uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    if (!read_file(input, output, capacity, actual_size)) return false;
    if (output != process::application_buffer) return true;
    return security_guard::allow_execution(input, output, actual_size);
}
} // namespace storage
