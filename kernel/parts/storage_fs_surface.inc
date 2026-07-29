enum class FsSurfaceClass : uint8_t {
    success,
    recovery,
    caller,
    missing,
    conflict,
    capacity,
    policy,
    corruption,
    unavailable,
    transport
};

constexpr FsSurfaceClass fs_surface_class(FsStatus status) {
    switch (status) {
        case FsStatus::ok: return FsSurfaceClass::success;
        case FsStatus::recovery_pending: return FsSurfaceClass::recovery;
        case FsStatus::invalid_argument:
        case FsStatus::invalid_path:
        case FsStatus::wrong_type:
            return FsSurfaceClass::caller;
        case FsStatus::not_found:
        case FsStatus::parent_missing:
            return FsSurfaceClass::missing;
        case FsStatus::already_exists:
        case FsStatus::directory_not_empty:
            return FsSurfaceClass::conflict;
        case FsStatus::buffer_too_small:
        case FsStatus::no_space:
            return FsSurfaceClass::capacity;
        case FsStatus::read_only:
        case FsStatus::permission_denied:
            return FsSurfaceClass::policy;
        case FsStatus::checksum_mismatch:
        case FsStatus::metadata_corrupt:
            return FsSurfaceClass::corruption;
        case FsStatus::not_mounted:
            return FsSurfaceClass::unavailable;
        case FsStatus::io_error:
            return FsSurfaceClass::transport;
    }
    return FsSurfaceClass::transport;
}

constexpr const char* fs_surface_class_name(FsSurfaceClass category) {
    switch (category) {
        case FsSurfaceClass::success: return "success";
        case FsSurfaceClass::recovery: return "recovery";
        case FsSurfaceClass::caller: return "caller";
        case FsSurfaceClass::missing: return "missing";
        case FsSurfaceClass::conflict: return "conflict";
        case FsSurfaceClass::capacity: return "capacity";
        case FsSurfaceClass::policy: return "policy";
        case FsSurfaceClass::corruption: return "corruption";
        case FsSurfaceClass::unavailable: return "unavailable";
        case FsSurfaceClass::transport: return "transport";
    }
    return "transport";
}

constexpr const char* fs_surface_message(FsStatus status) {
    switch (status) {
        case FsStatus::ok: return "Operation completed.";
        case FsStatus::recovery_pending: return "Committed; cleanup will finish during recovery.";
        case FsStatus::not_mounted: return "Persistent filesystem is unavailable.";
        case FsStatus::invalid_argument: return "Missing or invalid command argument.";
        case FsStatus::invalid_path: return "Path is invalid.";
        case FsStatus::not_found: return "Path was not found.";
        case FsStatus::wrong_type: return "Path has the wrong object type.";
        case FsStatus::already_exists: return "Destination already exists.";
        case FsStatus::parent_missing: return "Parent directory is missing.";
        case FsStatus::directory_not_empty: return "Directory is not empty.";
        case FsStatus::buffer_too_small: return "Output buffer is too small.";
        case FsStatus::no_space: return "Filesystem capacity or metadata slots are exhausted.";
        case FsStatus::read_only: return "Target is read-only.";
        case FsStatus::checksum_mismatch: return "File checksum verification failed.";
        case FsStatus::permission_denied: return "Operation was denied by security policy.";
        case FsStatus::metadata_corrupt: return "Filesystem metadata is corrupt.";
        case FsStatus::io_error: return "Block device I/O failed.";
    }
    return "Unknown filesystem failure.";
}

constexpr bool fs_surface_contract_valid() {
    return fs_surface_class(FsStatus::ok) == FsSurfaceClass::success &&
           fs_surface_class(FsStatus::recovery_pending) == FsSurfaceClass::recovery &&
           fs_surface_class(FsStatus::invalid_path) == FsSurfaceClass::caller &&
           fs_surface_class(FsStatus::not_found) == FsSurfaceClass::missing &&
           fs_surface_class(FsStatus::already_exists) == FsSurfaceClass::conflict &&
           fs_surface_class(FsStatus::no_space) == FsSurfaceClass::capacity &&
           fs_surface_class(FsStatus::permission_denied) == FsSurfaceClass::policy &&
           fs_surface_class(FsStatus::checksum_mismatch) == FsSurfaceClass::corruption &&
           fs_surface_class(FsStatus::not_mounted) == FsSurfaceClass::unavailable &&
           fs_surface_class(FsStatus::io_error) == FsSurfaceClass::transport;
}
