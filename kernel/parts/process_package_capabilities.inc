namespace process {
bool activate_external_capability_profile(const char* input, const uint8_t digest[security_guard::sha256_bytes],
                                          uint32_t mask, const char* read_scope, const char* write_scope) {
    if (!capability_ready || !input || !digest || (mask & ~capability_all)) {
        serial::line("ZENPKG_SYSCALL_PROFILE_REJECTED");
        return false;
    }
    const bool needs_read_scope = (mask & (capability_file_read | capability_file_stat)) != 0U;
    const bool needs_write_scope = (mask & capability_file_write) != 0U;
    if (needs_read_scope != (read_scope && *read_scope) || needs_write_scope != (write_scope && *write_scope)) {
        serial::line("ZENPKG_SYSCALL_SCOPE_REJECTED");
        return false;
    }
    char normalized[48]{};
    if (!normalize_capability_path(input, normalized)) return false;
    memset(&active_capabilities, 0, sizeof(active_capabilities));
    active_capabilities.valid = true;
    active_capabilities.mask = mask;
    storage::path_copy(active_capabilities.path, normalized, sizeof(active_capabilities.path));
    storage::path_copy(active_capabilities.read_scope, read_scope ? read_scope : "", sizeof(active_capabilities.read_scope));
    storage::path_copy(active_capabilities.write_scope, write_scope ? write_scope : "", sizeof(active_capabilities.write_scope));
    memcpy(active_capabilities.digest, digest, sizeof(active_capabilities.digest));
    capability_audit_failed = false;
    serial::write("ZENPKG_SYSCALL_PROFILE_ACTIVE app=");
    serial::write(active_capabilities.path);
    serial::write(" mask=");
    serial_hex(active_capabilities.mask);
    serial::write("\r\n");
    return true;
}
} // namespace process
