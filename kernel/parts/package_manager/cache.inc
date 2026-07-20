constexpr const char* cache_directory = "/var/cache/zp";
constexpr uint32_t cache_digest_prefix_bytes = 12U;

char cache_hex(uint8_t value) {
    return value < 10U ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10U);
}

bool repository_source_path(const package_repository::Target& target, char output[48]) {
    const char* basename = target.path;
    for (const char* cursor = target.path; *cursor; ++cursor) {
        if (*cursor == '/') basename = cursor + 1;
    }
    if (!*basename || !copy_text_exact(output, 48U, "/data/packages/")) return false;
    return native_package::append_text(output, 48U, basename);
}

bool cache_path(const package_repository::Target& target, bool partial, char output[48]) {
    if (!copy_text_exact(output, 48U, "/var/cache/zp/")) return false;
    uint32_t length = static_cast<uint32_t>(string_length(output));
    for (uint32_t i = 0; i < cache_digest_prefix_bytes; ++i) {
        if (length + 2U >= 48U) return false;
        output[length++] = cache_hex(static_cast<uint8_t>(target.package_digest[i] >> 4U));
        output[length++] = cache_hex(static_cast<uint8_t>(target.package_digest[i] & 0x0FU));
    }
    output[length] = 0;
    return native_package::append_text(output, 48U, partial ? ".part" : ".zpk");
}

bool package_matches_target(const native_package::VerifiedPackage& package, uint32_t package_size,
    const package_repository::Target& target) {
    if (!string_equal(package.manifest.name, target.name) ||
        !string_equal(package.manifest.version, target.version) ||
        !string_equal(package.manifest.entrypoint, target.entrypoint) ||
        package.manifest.payload_type != target.payload_type || package_size != target.package_length ||
        !crypto::digest_equal(package.package_digest, target.package_digest)) return false;
    uint8_t payload_digest[32]{};
    crypto::sha256(package.payload, package.payload_size, payload_digest);
    return crypto::digest_equal(payload_digest, target.payload_digest);
}

bool verified_target_file(const char* path, const package_repository::Target& target) {
    native_package::VerifiedPackage package{};
    uint32_t package_size = 0;
    const native_package::Error error = native_package::verify_file(
        path, package_buffer, storage::file_capacity(), package, package_size);
    return error == native_package::Error::none && package_matches_target(package, package_size, target);
}

void cache_report_target(const char* marker, const package_repository::Target& target) {
    serial::write(marker);
    serial::write(" name=");
    serial::write(target.name);
    serial::write(" version=");
    serial::line(target.version);
}

#include "transport.inc"

bool fetch_target(const package_repository::Target& target) {
    char final_path[48]{}, partial_path[48]{}, source_path[48]{};
    if (!cache_path(target, false, final_path) || !cache_path(target, true, partial_path) ||
        !repository_source_path(target, source_path)) {
        serial::line("ZENPKG_CACHE_PATH_INVALID");
        return false;
    }

    if (verified_target_file(final_path, target)) {
        cache_report_target("ZENPKG_CACHE_HIT", target);
        return true;
    }

    storage::FileInfo info{};
    if (storage::query(final_path, info) && !storage::package_cache_remove(final_path)) {
        serial::line("ZENPKG_CACHE_INVALID_OBJECT_REMOVE_FAILED");
        return false;
    }

    if (!transport_fetch_verified_source(source_path, partial_path, target)) {
        serial::line("ZENPKG_CACHE_TRANSPORT_REJECTED");
        return false;
    }

    if (!storage::package_cache_rename(partial_path, final_path) || !storage::sync_metadata() ||
        !verified_target_file(final_path, target)) {
        (void)storage::package_cache_remove(partial_path);
        (void)storage::package_cache_remove(final_path);
        (void)storage::sync_metadata();
        serial::line("ZENPKG_CACHE_COMMIT_REJECTED");
        return false;
    }

    cache_report_target("ZENPKG_CACHE_FETCH_COMMIT_OK", target);
    return true;
}

bool fetch_latest(const char* name) {
    const package_repository::Target* target = package_repository::latest_target(name);
    if (!target) {
        serial::line("ZENPKG_TARGET_NOT_FOUND");
        return false;
    }
    return fetch_target(*target);
}

bool cache_install_path(const package_repository::Target& target, char output[48]) {
    if (!fetch_target(target) || !cache_path(target, false, output) || !verified_target_file(output, target)) {
        serial::line("ZENPKG_CACHE_INSTALL_SOURCE_REJECTED");
        return false;
    }
    cache_report_target("ZENPKG_CACHE_SELECTED", target);
    return true;
}

void cache_status() {
    uint32_t valid = 0, invalid = 0;
    for (uint32_t i = 0; i < package_repository::target_count(); ++i) {
        const package_repository::Target* target = package_repository::target_at(i);
        if (!target) continue;
        char path[48]{};
        if (!cache_path(*target, false, path)) {
            ++invalid;
            continue;
        }
        storage::FileInfo info{};
        if (!storage::query(path, info)) continue;
        if (info.type == storage::zfs_type_file && info.size == target->package_length &&
            verified_target_file(path, *target)) ++valid;
        else ++invalid;
    }
    console::write("  Valid objects  ");
    console::unsigned_dec(valid);
    console::put('\n');
    console::write("  Invalid objects ");
    console::unsigned_dec(invalid);
    console::put('\n');
    console::line("  Commit model   verified resumable .part then atomic rename");
    serial::write("ZENPKG_CACHE_STATUS_OK valid=");
    process::serial_u32(valid);
    serial::write(" invalid=");
    process::serial_u32(invalid);
    serial::write("\r\n");
}

bool cache_verify() {
    uint32_t checked = 0;
    for (uint32_t i = 0; i < package_repository::target_count(); ++i) {
        const package_repository::Target* target = package_repository::target_at(i);
        if (!target) continue;
        char path[48]{};
        if (!cache_path(*target, false, path)) return false;
        storage::FileInfo info{};
        if (!storage::query(path, info)) continue;
        ++checked;
        if (info.type != storage::zfs_type_file || info.size != target->package_length ||
            !verified_target_file(path, *target)) {
            serial::line("ZENPKG_CACHE_VERIFY_REJECTED");
            return false;
        }
    }
    serial::write("ZENPKG_CACHE_VERIFY_OK objects=");
    process::serial_u32(checked);
    serial::write("\r\n");
    return true;
}

bool cache_clean() {
    uint32_t removed = 0;
    for (uint32_t i = 0; i < package_repository::target_count(); ++i) {
        const package_repository::Target* target = package_repository::target_at(i);
        if (!target) continue;
        for (uint32_t partial = 0; partial < 2U; ++partial) {
            char path[48]{};
            if (!cache_path(*target, partial != 0U, path)) return false;
            storage::FileInfo info{};
            if (!storage::query(path, info)) continue;
            if (!storage::package_cache_remove(path)) {
                serial::line("ZENPKG_CACHE_CLEAN_REJECTED");
                return false;
            }
            ++removed;
        }
    }
    if (removed && !storage::sync_metadata()) {
        serial::line("ZENPKG_CACHE_CLEAN_SYNC_FAILED");
        return false;
    }
    serial::write("ZENPKG_CACHE_CLEAN_OK removed=");
    process::serial_u32(removed);
    serial::write("\r\n");
    return true;
}
