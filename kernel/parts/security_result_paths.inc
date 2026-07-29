namespace storage {

FsResult guarded_remove_result(const char* path) {
    if (!path || !*path) return fs_failure(FsOperation::remove, FsStatus::invalid_argument);
    if (security_policy::protected_path(path)) {
        security_policy::report_protected_path("remove", path);
        return fs_failure(FsOperation::remove, FsStatus::permission_denied);
    }
    if (!zrwp::authorize_remove(path)) {
        return fs_failure(FsOperation::remove, FsStatus::permission_denied);
    }
    return remove_result(path);
}

FsResult guarded_rename_entry_result(const char* source, const char* destination) {
    if (!source || !destination || !*source || !*destination) {
        return fs_failure(FsOperation::rename, FsStatus::invalid_argument);
    }
    if (security_policy::protected_path(source) || security_policy::protected_path(destination)) {
        security_policy::report_protected_path(
            "rename", security_policy::protected_path(source) ? source : destination);
        return fs_failure(FsOperation::rename, FsStatus::permission_denied);
    }
    if (!security_guard::allow_persistent_transfer(source, destination) ||
        !zrwp::authorize_rename(source, destination)) {
        return fs_failure(FsOperation::rename, FsStatus::permission_denied);
    }
    return rename_entry_result(source, destination);
}

FsResult guarded_copy_file_result(const char* source, const char* destination) {
    if (!source || !destination || !*source || !*destination) {
        return fs_failure(FsOperation::copy, FsStatus::invalid_argument);
    }
    if (security_policy::protected_path(destination)) {
        security_policy::report_protected_path("copy-destination", destination);
        return fs_failure(FsOperation::copy, FsStatus::permission_denied);
    }
    if (!security_guard::allow_persistent_transfer(source, destination)) {
        return fs_failure(FsOperation::copy, FsStatus::permission_denied);
    }
    FileInfo info{};
    const FsResult queried = query_result(source, info);
    if (!queried.ok()) return fs_result(FsOperation::copy, queried.status, 0U, queried.block);
    if (info.type != zfs_type_file) return fs_failure(FsOperation::copy, FsStatus::wrong_type);
    if (!zrwp::authorize_copy(destination, info.size)) {
        return fs_failure(FsOperation::copy, FsStatus::permission_denied);
    }
    return copy_file_result(source, destination);
}

FsResult package_write_file_result(const char* path, const uint8_t* data, uint32_t size, bool append) {
    if (!path || (!data && size)) return fs_failure(FsOperation::write, FsStatus::invalid_argument);
    if (append || !security_policy::package_managed_path(path)) {
        security_policy::report_protected_path("package-write-rejected", path);
        return fs_failure(FsOperation::write, FsStatus::permission_denied);
    }
    if (!security_guard::allow_persistent_write(path, data, size, false)) {
        return fs_failure(FsOperation::write, FsStatus::permission_denied);
    }
    return write_file_result(path, data, size, false);
}

FsResult package_cache_write_partial_result(const char* path, const uint8_t* data,
                                            uint32_t size, bool append) {
    if (!path || (!data && size)) return fs_failure(FsOperation::write, FsStatus::invalid_argument);
    if (!security_policy::package_cache_partial_path(path)) {
        security_policy::report_protected_path("package-cache-partial-write-rejected", path);
        return fs_failure(FsOperation::write, FsStatus::permission_denied);
    }
    if (!security_guard::allow_persistent_write(path, data, size, append)) {
        return fs_failure(FsOperation::write, FsStatus::permission_denied);
    }
    return write_file_result(path, data, size, append);
}

FsResult package_remove_result(const char* path) {
    if (!security_policy::package_payload_path(path)) {
        security_policy::report_protected_path("package-remove-rejected", path);
        return fs_failure(FsOperation::remove, FsStatus::permission_denied);
    }
    return remove_result(path);
}

FsResult package_transport_remove_result(const char* path) {
    if (!security_policy::package_transport_state_path(path)) {
        security_policy::report_protected_path("package-transport-remove-rejected", path);
        return fs_failure(FsOperation::remove, FsStatus::permission_denied);
    }
    return remove_result(path);
}

FsResult package_cache_remove_result(const char* path) {
    if (!security_policy::package_cache_object_path(path)) {
        security_policy::report_protected_path("package-cache-remove-rejected", path);
        return fs_failure(FsOperation::remove, FsStatus::permission_denied);
    }
    return remove_result(path);
}

FsResult package_cache_rename_result(const char* source, const char* destination) {
    if (!security_policy::package_cache_object_path(source) ||
        !security_policy::package_cache_object_path(destination)) {
        security_policy::report_protected_path(
            "package-cache-rename-rejected",
            security_policy::package_cache_object_path(source) ? destination : source);
        return fs_failure(FsOperation::rename, FsStatus::permission_denied);
    }
    if (!security_guard::allow_persistent_transfer(source, destination)) {
        return fs_failure(FsOperation::rename, FsStatus::permission_denied);
    }
    return rename_entry_result(source, destination);
}

bool package_write_file_recorded(const char* path, const uint8_t* data, uint32_t size, bool append) {
    return fs_record_result(package_write_file_result(path, data, size, append)).ok();
}
bool package_cache_write_partial_recorded(const char* path, const uint8_t* data,
                                          uint32_t size, bool append) {
    return fs_record_result(package_cache_write_partial_result(path, data, size, append)).ok();
}
bool package_remove_recorded(const char* path) {
    return fs_record_result(package_remove_result(path)).ok();
}
bool package_transport_remove_recorded(const char* path) {
    return fs_record_result(package_transport_remove_result(path)).ok();
}
bool package_cache_remove_recorded(const char* path) {
    return fs_record_result(package_cache_remove_result(path)).ok();
}
bool package_cache_rename_recorded(const char* source, const char* destination) {
    return fs_record_result(package_cache_rename_result(source, destination)).ok();
}
bool sync_metadata_recorded() {
    return fs_record_result(sync_metadata_result()).ok();
}

[[maybe_unused]] void fs_surface_reference_legacy_package_adapters() {
    (void)&package_write_file;
    (void)&package_cache_write_partial;
    (void)&package_remove;
    (void)&package_transport_remove;
    (void)&package_cache_remove;
    (void)&package_cache_rename;
}

} // namespace storage
