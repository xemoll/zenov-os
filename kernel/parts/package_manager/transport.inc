constexpr uint32_t transport_chunk_bytes = 512U;
constexpr uint32_t transport_retry_limit = 3U;

void transport_report(const char* marker, const package_repository::Target& target,
    uint32_t offset, uint32_t attempt) {
    serial::write(marker);
    serial::write(" name=");
    serial::write(target.name);
    serial::write(" version=");
    serial::write(target.version);
    serial::write(" offset=");
    process::serial_u32(offset);
    serial::write(" expected=");
    process::serial_u32(target.package_length);
    serial::write(" attempt=");
    process::serial_u32(attempt);
    serial::write("\r\n");
}

bool transport_remove_partial(const char* partial_path) {
    storage::FileInfo info{};
    if (!storage::query(partial_path, info)) return true;
    return storage::package_cache_remove(partial_path) && storage::sync_metadata();
}

bool transport_load_verified_source(const char* source_path, const package_repository::Target& target,
    uint32_t& source_size) {
    native_package::VerifiedPackage package{};
    source_size = 0;
    const native_package::Error error = native_package::verify_file(
        source_path, package_buffer, maximum_package_bytes, package, source_size);
    return error == native_package::Error::none && package_matches_target(package, source_size, target);
}

bool transport_prepare_attempt(const char* source_path, const char* partial_path,
    const package_repository::Target& target, uint32_t& source_size, uint32_t& offset) {
    offset = 0;
    if (!transport_load_verified_source(source_path, target, source_size)) {
        serial::line("ZENPKG_TRANSPORT_SOURCE_REJECTED");
        return false;
    }

    storage::FileInfo partial_info{};
    if (!storage::query(partial_path, partial_info)) return true;
    if (partial_info.type != storage::zfs_type_file || partial_info.size > source_size ||
        partial_info.size > target.package_length) {
        if (!transport_remove_partial(partial_path) ||
            !transport_load_verified_source(source_path, target, source_size)) {
            serial::line("ZENPKG_TRANSPORT_PARTIAL_RESET_FAILED");
            return false;
        }
        serial::line("ZENPKG_TRANSPORT_PARTIAL_RESET");
        return true;
    }

    if (partial_info.size == 0U) return true;

    uint8_t source_prefix_digest[32]{};
    crypto::sha256(package_buffer, partial_info.size, source_prefix_digest);
    uint32_t partial_size = 0;
    if (!storage::read_file(partial_path, package_buffer, maximum_package_bytes, partial_size) ||
        partial_size != partial_info.size) {
        if (!transport_remove_partial(partial_path) ||
            !transport_load_verified_source(source_path, target, source_size)) {
            serial::line("ZENPKG_TRANSPORT_PARTIAL_READ_FAILED");
            return false;
        }
        serial::line("ZENPKG_TRANSPORT_PARTIAL_RESET");
        return true;
    }

    uint8_t partial_digest[32]{};
    crypto::sha256(package_buffer, partial_size, partial_digest);
    const bool prefix_valid = crypto::digest_equal(source_prefix_digest, partial_digest);
    if (!transport_load_verified_source(source_path, target, source_size)) {
        serial::line("ZENPKG_TRANSPORT_SOURCE_RELOAD_FAILED");
        return false;
    }
    if (!prefix_valid) {
        if (!transport_remove_partial(partial_path)) {
            serial::line("ZENPKG_TRANSPORT_PARTIAL_RESET_FAILED");
            return false;
        }
        serial::line("ZENPKG_TRANSPORT_PARTIAL_RESET");
        return true;
    }

    offset = partial_size;
    return true;
}

bool transport_fetch_verified_source(const char* source_path, const char* partial_path,
    const package_repository::Target& target) {
    for (uint32_t attempt = 1U; attempt <= transport_retry_limit; ++attempt) {
        uint32_t source_size = 0, offset = 0;
        if (!transport_prepare_attempt(source_path, partial_path, target, source_size, offset)) return false;
        transport_report(offset ? "ZENPKG_TRANSPORT_RESUME" : "ZENPKG_TRANSPORT_BEGIN",
            target, offset, attempt);

        bool attempt_failed = false;
        while (offset < source_size) {
            uint32_t chunk = source_size - offset;
            if (chunk > transport_chunk_bytes) chunk = transport_chunk_bytes;
            if (!storage::package_cache_write_partial(
                    partial_path, package_buffer + offset, chunk, offset != 0U) ||
                !storage::sync_metadata()) {
                attempt_failed = true;
                break;
            }
            storage::FileInfo committed{};
            if (!storage::query(partial_path, committed) || committed.type != storage::zfs_type_file ||
                committed.size != offset + chunk || committed.size > target.package_length) {
                attempt_failed = true;
                break;
            }
            offset += chunk;
            transport_report("ZENPKG_TRANSPORT_CHUNK_COMMIT", target, offset, attempt);
        }

        if (!attempt_failed && offset == source_size && source_size == target.package_length &&
            verified_target_file(partial_path, target)) {
            transport_report("ZENPKG_TRANSPORT_COMPLETE", target, offset, attempt);
            return true;
        }

        if (attempt < transport_retry_limit) {
            transport_report("ZENPKG_TRANSPORT_RETRY", target, offset, attempt);
            continue;
        }
    }

    serial::line("ZENPKG_TRANSPORT_RETRY_EXHAUSTED");
    return false;
}
