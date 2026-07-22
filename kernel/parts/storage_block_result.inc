enum class BlockOperation : uint8_t {
    none,
    identify,
    read,
    write,
    flush,
    reset
};

enum class BlockStatus : uint8_t {
    ok,
    no_device,
    invalid_argument,
    out_of_range,
    read_only,
    timeout_busy,
    timeout_drq,
    device_fault,
    command_aborted,
    id_not_found,
    uncorrectable,
    interface_crc,
    device_error,
    reset_failed
};

struct BlockResult {
    BlockOperation operation;
    BlockStatus status;
    uint32_t lba;
    uint8_t attempts;
    uint8_t status_register;
    uint8_t error_register;

    constexpr bool ok() const { return status == BlockStatus::ok; }
};

constexpr BlockResult block_result(BlockOperation operation, BlockStatus status, uint32_t lba,
                                   uint8_t attempts, uint8_t status_register, uint8_t error_register) {
    return BlockResult{operation, status, lba, attempts, status_register, error_register};
}

constexpr BlockResult block_success(BlockOperation operation, uint32_t lba, uint8_t attempts = 1U) {
    return block_result(operation, BlockStatus::ok, lba, attempts, 0U, 0U);
}

constexpr bool block_retryable(BlockStatus status) {
    return status == BlockStatus::timeout_busy || status == BlockStatus::timeout_drq ||
           status == BlockStatus::device_fault || status == BlockStatus::command_aborted ||
           status == BlockStatus::interface_crc;
}

constexpr const char* block_operation_name(BlockOperation operation) {
    switch (operation) {
        case BlockOperation::none: return "none";
        case BlockOperation::identify: return "identify";
        case BlockOperation::read: return "read";
        case BlockOperation::write: return "write";
        case BlockOperation::flush: return "flush";
        case BlockOperation::reset: return "reset";
    }
    return "unknown";
}

constexpr const char* block_status_name(BlockStatus status) {
    switch (status) {
        case BlockStatus::ok: return "ok";
        case BlockStatus::no_device: return "no-device";
        case BlockStatus::invalid_argument: return "invalid-argument";
        case BlockStatus::out_of_range: return "out-of-range";
        case BlockStatus::read_only: return "read-only";
        case BlockStatus::timeout_busy: return "timeout-busy";
        case BlockStatus::timeout_drq: return "timeout-drq";
        case BlockStatus::device_fault: return "device-fault";
        case BlockStatus::command_aborted: return "command-aborted";
        case BlockStatus::id_not_found: return "id-not-found";
        case BlockStatus::uncorrectable: return "uncorrectable";
        case BlockStatus::interface_crc: return "interface-crc";
        case BlockStatus::device_error: return "device-error";
        case BlockStatus::reset_failed: return "reset-failed";
    }
    return "unknown";
}
