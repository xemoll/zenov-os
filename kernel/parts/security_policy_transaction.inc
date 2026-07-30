namespace security_policy_transaction {
constexpr uint32_t workspace_bytes = 16U * 1024U;
#ifdef SECURITY_POLICY_TRANSACTION_HOST_TEST
alignas(16) uint8_t host_rollback_data[workspace_bytes]{};
uint8_t* rollback_data = host_rollback_data;
#else
constexpr uintptr_t workspace_address = supervisor_layout::policy_transaction;
static_assert(workspace_bytes == supervisor_layout::policy_transaction_bytes);
static_assert(workspace_address >= supervisor_layout::pmm_bitmap + supervisor_layout::pmm_bitmap_bytes);
static_assert(workspace_address + workspace_bytes <= supervisor_layout::user_base);
uint8_t* rollback_data = reinterpret_cast<uint8_t*>(workspace_address);
#endif
bool busy = false;

void wipe() {
    volatile uint8_t* output = rollback_data;
    for (uint32_t i = 0U; i < workspace_bytes; ++i) output[i] = 0U;
}

bool acquire(const char* live_path, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (busy || !live_path || !*live_path || !capacity || capacity > workspace_bytes) return false;
    busy = true;
    if (!storage::read_file(live_path, rollback_data, capacity, size) || !size || size > capacity) {
        wipe();
        busy = false;
        size = 0U;
        return false;
    }
    return true;
}

bool restore_verified(const char* live_path, uint32_t size, uint8_t* readback, uint32_t capacity) {
    if (!busy || !live_path || !*live_path || !size || size > workspace_bytes || !readback || capacity < size) return false;
    if (!storage::write_file(live_path, rollback_data, size, false)) return false;
    uint32_t actual = 0U;
    if (!storage::read_file(live_path, readback, capacity, actual) || actual != size) return false;
    return storage::bytes_equal(readback, rollback_data, size);
}

bool version_matches(const char* version_path, uint32_t expected) {
    uint8_t data[16]{};
    uint32_t size = 0U, parsed = 0U;
    return version_path && *version_path && expected &&
        storage::read_file(version_path, data, sizeof(data), size) &&
        security_policy_format::parse_nonzero_decimal_u32(data, size, parsed) && parsed == expected;
}

void release() {
    wipe();
    busy = false;
}
} // namespace security_policy_transaction
