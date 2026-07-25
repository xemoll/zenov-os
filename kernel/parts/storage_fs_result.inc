enum class FsOperation : uint8_t {
    none,
    mount,
    recover,
    read,
    write,
    sync,
    query,
    create,
    remove,
    rename,
    copy
};

enum class FsStatus : uint8_t {
    ok,
    recovery_pending,
    not_mounted,
    invalid_argument,
    invalid_path,
    not_found,
    wrong_type,
    already_exists,
    parent_missing,
    directory_not_empty,
    buffer_too_small,
    no_space,
    read_only,
    checksum_mismatch,
    permission_denied,
    metadata_corrupt,
    io_error
};

struct FsResult {
    FsOperation operation;
    FsStatus status;
    uint32_t bytes;
    BlockResult block;

    constexpr bool ok() const {
        return status == FsStatus::ok || status == FsStatus::recovery_pending;
    }
    constexpr bool durable() const { return status == FsStatus::ok; }
    constexpr bool has_block_error() const { return !block.ok(); }
};

constexpr FsResult fs_result(FsOperation operation, FsStatus status, uint32_t bytes,
                             const BlockResult& block) {
    return FsResult{operation, status, bytes, block};
}

constexpr FsResult fs_success(FsOperation operation, uint32_t bytes = 0U) {
    return fs_result(operation, FsStatus::ok, bytes,
                     block_success(BlockOperation::none, 0U, 0U));
}

constexpr FsResult fs_recovery_pending(FsOperation operation, uint32_t bytes,
                                       const BlockResult& block) {
    return fs_result(operation, FsStatus::recovery_pending, bytes, block);
}

constexpr FsResult fs_failure(FsOperation operation, FsStatus status, uint32_t bytes = 0U) {
    return fs_result(operation, status, bytes,
                     block_success(BlockOperation::none, 0U, 0U));
}

constexpr FsStatus fs_status_from_block(BlockStatus status) {
    switch (status) {
        case BlockStatus::ok: return FsStatus::ok;
        case BlockStatus::no_device: return FsStatus::not_mounted;
        case BlockStatus::invalid_argument: return FsStatus::invalid_argument;
        case BlockStatus::out_of_range: return FsStatus::metadata_corrupt;
        case BlockStatus::read_only: return FsStatus::read_only;
        case BlockStatus::timeout_busy:
        case BlockStatus::timeout_drq:
        case BlockStatus::device_fault:
        case BlockStatus::command_aborted:
        case BlockStatus::id_not_found:
        case BlockStatus::uncorrectable:
        case BlockStatus::interface_crc:
        case BlockStatus::device_error:
        case BlockStatus::reset_failed:
            return FsStatus::io_error;
    }
    return FsStatus::io_error;
}

constexpr FsResult fs_from_block(FsOperation operation, const BlockResult& block,
                                 uint32_t bytes = 0U) {
    return fs_result(operation, fs_status_from_block(block.status), bytes, block);
}

constexpr bool fs_retryable(const FsResult& result) {
    return result.status == FsStatus::io_error && block_retryable(result.block.status);
}

constexpr const char* fs_operation_name(FsOperation operation) {
    switch (operation) {
        case FsOperation::none: return "none";
        case FsOperation::mount: return "mount";
        case FsOperation::recover: return "recover";
        case FsOperation::read: return "read";
        case FsOperation::write: return "write";
        case FsOperation::sync: return "sync";
        case FsOperation::query: return "query";
        case FsOperation::create: return "create";
        case FsOperation::remove: return "remove";
        case FsOperation::rename: return "rename";
        case FsOperation::copy: return "copy";
    }
    return "unknown";
}

constexpr const char* fs_status_name(FsStatus status) {
    switch (status) {
        case FsStatus::ok: return "ok";
        case FsStatus::recovery_pending: return "recovery-pending";
        case FsStatus::not_mounted: return "not-mounted";
        case FsStatus::invalid_argument: return "invalid-argument";
        case FsStatus::invalid_path: return "invalid-path";
        case FsStatus::not_found: return "not-found";
        case FsStatus::wrong_type: return "wrong-type";
        case FsStatus::already_exists: return "already-exists";
        case FsStatus::parent_missing: return "parent-missing";
        case FsStatus::directory_not_empty: return "directory-not-empty";
        case FsStatus::buffer_too_small: return "buffer-too-small";
        case FsStatus::no_space: return "no-space";
        case FsStatus::read_only: return "read-only";
        case FsStatus::checksum_mismatch: return "checksum-mismatch";
        case FsStatus::permission_denied: return "permission-denied";
        case FsStatus::metadata_corrupt: return "metadata-corrupt";
        case FsStatus::io_error: return "io-error";
    }
    return "unknown";
}
