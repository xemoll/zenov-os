constexpr uint32_t fs_status_to_syscall_error(storage::FsStatus status) {
    switch (status) {
        case storage::FsStatus::ok:
        case storage::FsStatus::recovery_pending:
            return 0U;
        case storage::FsStatus::not_found:
            return error_not_found;
        case storage::FsStatus::buffer_too_small:
        case storage::FsStatus::no_space:
            return error_no_space;
        case storage::FsStatus::read_only:
        case storage::FsStatus::permission_denied:
            return error_denied;
        case storage::FsStatus::checksum_mismatch:
        case storage::FsStatus::metadata_corrupt:
            return error_corrupt;
        case storage::FsStatus::not_mounted:
        case storage::FsStatus::io_error:
            return error_io;
        case storage::FsStatus::invalid_argument:
        case storage::FsStatus::invalid_path:
        case storage::FsStatus::wrong_type:
        case storage::FsStatus::already_exists:
        case storage::FsStatus::parent_missing:
        case storage::FsStatus::directory_not_empty:
            return error_invalid;
    }
    return error_io;
}

constexpr bool fs_status_error_mapping_valid() {
    return fs_status_to_syscall_error(storage::FsStatus::not_found) == error_not_found &&
           fs_status_to_syscall_error(storage::FsStatus::buffer_too_small) == error_no_space &&
           fs_status_to_syscall_error(storage::FsStatus::permission_denied) == error_denied &&
           fs_status_to_syscall_error(storage::FsStatus::checksum_mismatch) == error_corrupt &&
           fs_status_to_syscall_error(storage::FsStatus::io_error) == error_io &&
           fs_status_to_syscall_error(storage::FsStatus::invalid_path) == error_invalid;
}
