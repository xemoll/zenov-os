enum class InspectionResult : uint8_t { clean, matched, unsupported };

InspectionResult inspect_payload(const char* path,
                                 const uint8_t* data,
                                 uint32_t size,
                                 uint32_t archive_depth,
                                 uint8_t* scratch,
                                 uint8_t* alternate,
                                 security_guard::ScanResult& result);

bool find_zip_eocd(const uint8_t* data, uint32_t size, uint32_t& eocd_offset) {
    eocd_offset = 0U;
    if (!data || size < 22U) return false;
    const uint32_t minimum = size > 65557U ? size - 65557U : 0U;
    uint32_t cursor = size - 22U;
    for (;;) {
        if (load_le32(data + cursor) == 0x06054B50U) {
            const uint16_t comment_size = load_le16(data + cursor + 20U);
            if (range_fits(cursor, 22U + comment_size, size) && cursor + 22U + comment_size == size) {
                eocd_offset = cursor;
                return true;
            }
        }
        if (cursor == minimum) break;
        --cursor;
    }
    return false;
}

bool looks_like_zip(const uint8_t* data, uint32_t size) {
    if (!data || size < 4U) return false;
    if (load_le32(data) == 0x04034B50U) return true;
    uint32_t eocd_offset = 0U;
    return find_zip_eocd(data, size, eocd_offset);
}

bool path_has_zip_extension(const char* path) {
    if (!path) return false;
    constexpr uint32_t max_path_scan = 1024U;
    uint32_t length = 0U;
    while (length < max_path_scan && path[length]) ++length;
    if (length < 4U || length == max_path_scan) return false;
    const char a = path[length - 3U], b = path[length - 2U], c = path[length - 1U];
    return path[length - 4U] == '.' && (a == 'z' || a == 'Z') &&
        (b == 'i' || b == 'I') && (c == 'p' || c == 'P');
}

bool looks_like_zlib_stream(const uint8_t* data, uint32_t size) {
    return data && size >= 6U && zlib_header_valid(data, size);
}

struct ZipEntryView {
    uint32_t local_offset = 0U;
    uint32_t payload_offset = 0U;
    uint32_t payload_end = 0U;
    uint32_t compressed_size = 0U;
    uint32_t expanded_size = 0U;
    uint32_t expected_crc = 0U;
    uint16_t method = 0U;
};

InspectionResult inspect_zip(const char* path,
                             const uint8_t* data,
                             uint32_t size,
                             uint32_t archive_depth,
                             uint8_t* output,
                             uint8_t* alternate,
                             security_guard::ScanResult& result) {
    if (archive_depth >= max_archive_depth || !data || size < 22U) return InspectionResult::unsupported;

    uint32_t eocd_offset = 0U;
    if (!find_zip_eocd(data, size, eocd_offset)) return InspectionResult::unsupported;
    const uint16_t disk_number = load_le16(data + eocd_offset + 4U);
    const uint16_t central_disk = load_le16(data + eocd_offset + 6U);
    const uint16_t disk_entries = load_le16(data + eocd_offset + 8U);
    const uint16_t total_entries = load_le16(data + eocd_offset + 10U);
    const uint32_t central_size = load_le32(data + eocd_offset + 12U);
    const uint32_t central_relative = load_le32(data + eocd_offset + 16U);
    if (disk_number || central_disk || disk_entries != total_entries || total_entries > max_archive_entries ||
        central_size > eocd_offset) return InspectionResult::unsupported;

    const uint32_t central_offset = eocd_offset - central_size;
    if (central_relative > central_offset) return InspectionResult::unsupported;
    const uint32_t archive_base = central_offset - central_relative;
    ZipEntryView entries[max_archive_entries]{};
    uint32_t central_cursor = central_offset;

    for (uint32_t entry_index = 0U; entry_index < total_entries; ++entry_index) {
        if (!range_fits(central_cursor, 46U, eocd_offset) ||
            load_le32(data + central_cursor) != 0x02014B50U) return InspectionResult::unsupported;
        const uint16_t version_needed = load_le16(data + central_cursor + 6U);
        const uint16_t flags = load_le16(data + central_cursor + 8U);
        const uint16_t method = load_le16(data + central_cursor + 10U);
        const uint32_t expected_crc = load_le32(data + central_cursor + 16U);
        const uint32_t compressed_size = load_le32(data + central_cursor + 20U);
        const uint32_t expanded_size = load_le32(data + central_cursor + 24U);
        const uint16_t name_size = load_le16(data + central_cursor + 28U);
        const uint16_t extra_size = load_le16(data + central_cursor + 30U);
        const uint16_t comment_size = load_le16(data + central_cursor + 32U);
        const uint16_t start_disk = load_le16(data + central_cursor + 34U);
        const uint32_t local_relative = load_le32(data + central_cursor + 42U);
        const uint32_t variable_size = static_cast<uint32_t>(name_size) + extra_size + comment_size;
        if (version_needed > 20U || (flags & static_cast<uint16_t>(~0x0806U)) != 0U ||
            (method != 8U && (flags & 0x0006U)) || start_disk || compressed_size == 0xFFFFFFFFU ||
            expanded_size == 0xFFFFFFFFU || local_relative == 0xFFFFFFFFU ||
            expanded_size > workspace_bytes || !range_fits(central_cursor + 46U, variable_size, eocd_offset) ||
            local_relative > central_offset - archive_base) return InspectionResult::unsupported;

        const uint32_t local_offset = archive_base + local_relative;
        if (!range_fits(local_offset, 30U, central_offset) ||
            load_le32(data + local_offset) != 0x04034B50U) return InspectionResult::unsupported;
        const uint16_t local_version_needed = load_le16(data + local_offset + 4U);
        const uint16_t local_flags = load_le16(data + local_offset + 6U);
        const uint16_t local_method = load_le16(data + local_offset + 8U);
        const uint32_t local_crc = load_le32(data + local_offset + 14U);
        const uint32_t local_compressed = load_le32(data + local_offset + 18U);
        const uint32_t local_expanded = load_le32(data + local_offset + 22U);
        const uint16_t local_name_size = load_le16(data + local_offset + 26U);
        const uint16_t local_extra_size = load_le16(data + local_offset + 28U);
        if (local_version_needed != version_needed || local_flags != flags || local_method != method ||
            local_crc != expected_crc ||
            local_compressed != compressed_size || local_expanded != expanded_size ||
            local_name_size != name_size ||
            !range_fits(local_offset + 30U, static_cast<uint32_t>(local_name_size) + local_extra_size, central_offset) ||
            !storage::bytes_equal(data + local_offset + 30U, data + central_cursor + 46U, name_size)) {
            return InspectionResult::unsupported;
        }
        const uint32_t payload_offset = local_offset + 30U + local_name_size + local_extra_size;
        if (!range_fits(payload_offset, compressed_size, central_offset)) return InspectionResult::unsupported;
        for (uint32_t previous = 0U; previous < entry_index; ++previous) {
            if (entries[previous].local_offset == local_offset) return InspectionResult::unsupported;
        }
        entries[entry_index] = ZipEntryView{local_offset, payload_offset,
            payload_offset + compressed_size, compressed_size, expanded_size, expected_crc, method};
        central_cursor += 46U + variable_size;
    }
    if (central_cursor != eocd_offset) return InspectionResult::unsupported;
    if (!total_entries && central_size != 0U) return InspectionResult::unsupported;
    if (archive_base) {
        const InspectionResult prefix = inspect_payload(path, data, archive_base,
            max_archive_depth, output, nullptr, result);
        if (prefix != InspectionResult::clean) return prefix;
    }
    if (!total_entries) return InspectionResult::clean;

    for (uint32_t i = 1U; i < total_entries; ++i) {
        ZipEntryView value = entries[i];
        uint32_t position = i;
        while (position && entries[position - 1U].local_offset > value.local_offset) {
            entries[position] = entries[position - 1U];
            --position;
        }
        entries[position] = value;
    }
    if (entries[0].local_offset != archive_base) return InspectionResult::unsupported;
    for (uint32_t i = 1U; i < total_entries; ++i) {
        if (entries[i].local_offset != entries[i - 1U].payload_end) return InspectionResult::unsupported;
    }
    if (entries[total_entries - 1U].payload_end != central_offset) return InspectionResult::unsupported;

    uint32_t total_expanded = 0U;
    for (uint32_t entry_index = 0U; entry_index < total_entries; ++entry_index) {
        const ZipEntryView& entry = entries[entry_index];
        if (entry.expanded_size > max_expanded_total - total_expanded ||
            !expansion_within_limit(entry.compressed_size, entry.expanded_size)) return InspectionResult::unsupported;
        uint32_t actual_size = 0U;
        if (entry.method == 0U) {
            if (entry.compressed_size != entry.expanded_size) return InspectionResult::unsupported;
            if (entry.expanded_size) memcpy(output, data + entry.payload_offset, entry.expanded_size);
            actual_size = entry.expanded_size;
        } else if (entry.method == 8U) {
            if (!inflate_raw(data + entry.payload_offset, entry.compressed_size,
                    output, workspace_bytes, actual_size) || actual_size != entry.expanded_size) {
                return InspectionResult::unsupported;
            }
        } else return InspectionResult::unsupported;

        if (crc32(output, actual_size) != entry.expected_crc) return InspectionResult::unsupported;
        total_expanded += actual_size;
        if (classify_decoded(path, output, actual_size, result,
                archive_depth ? "ZIP2" : "ZIP")) return InspectionResult::matched;
        uint8_t* nested_alternate = alternate;
        if (data == alternate && entry_index + 1U < total_entries) nested_alternate = nullptr;
        const InspectionResult nested = inspect_payload(path, output, actual_size,
            archive_depth + 1U, output, nested_alternate, result);
        if (nested != InspectionResult::clean) return nested;
    }
    return InspectionResult::clean;
}

InspectionResult inspect_decoded_payload(const char* path,
                                         uint8_t* decoded,
                                         uint32_t decoded_size,
                                         uint32_t archive_depth,
                                         uint8_t* alternate,
                                         security_guard::ScanResult& result,
                                         const char* layer) {
    if (classify_decoded(path, decoded, decoded_size, result, layer)) return InspectionResult::matched;
    if (looks_like_zip(decoded, decoded_size) || looks_like_gzip(decoded, decoded_size) ||
        looks_like_zlib_stream(decoded, decoded_size)) {
        const InspectionResult archive = inspect_payload(path, decoded, decoded_size,
            archive_depth + 1U, decoded, alternate, result);
        if (archive != InspectionResult::clean) return archive;
    }
    if (text_case_candidate(decoded, decoded_size)) {
        uppercase_copy(decoded, decoded_size, decoded);
        if (classify_decoded(path, decoded, decoded_size, result, "Base64Case")) {
            return InspectionResult::matched;
        }
    }
    return InspectionResult::clean;
}

InspectionResult inspect_embedded_base64(const char* path,
                                         const uint8_t* data,
                                         uint32_t size,
                                         uint32_t archive_depth,
                                         uint8_t* scratch,
                                         uint8_t* alternate,
                                         security_guard::ScanResult& result) {
    if (!data) return size ? InspectionResult::unsupported : InspectionResult::clean;
    uint32_t cursor = 0U;
    while (cursor < size) {
        while (cursor < size && base64_value(data[cursor]) < 0) ++cursor;
        const uint32_t start = cursor;
        while (cursor < size && base64_value(data[cursor]) >= 0) ++cursor;
        uint32_t padding = 0U;
        while (cursor < size && data[cursor] == '=' && padding < 2U) { ++cursor; ++padding; }
        const uint32_t token_size = cursor - start;
        if (token_size < 16U || token_size == size) continue;
        uint32_t decoded_size = 0U;
        if (!decode_base64(data + start, token_size, scratch, workspace_bytes, decoded_size) || !decoded_size) continue;
        const InspectionResult decoded = inspect_decoded_payload(path, scratch, decoded_size,
            archive_depth, alternate, result, "Base64Token");
        if (decoded != InspectionResult::clean) return decoded;
    }
    return InspectionResult::clean;
}

InspectionResult inspect_wrapped_deflate_payload(const char* path,
                                                 const uint8_t* data,
                                                 uint32_t size,
                                                 uint32_t archive_depth,
                                                 uint8_t* scratch,
                                                 uint8_t* alternate,
                                                 security_guard::ScanResult& result,
                                                 bool gzip) {
    if (archive_depth >= max_archive_depth) return InspectionResult::unsupported;
    uint8_t* output = scratch;
    uint8_t* nested_alternate = alternate;
    if (!output || output == data) {
        output = alternate;
        nested_alternate = scratch;
    }
    if (!output || output == data) return InspectionResult::unsupported;

    uint32_t decoded_size = 0U;
    const WrappedDeflateResult decoded = gzip
        ? decode_gzip(data, size, output, workspace_bytes, decoded_size)
        : decode_zlib(data, size, output, workspace_bytes, decoded_size);
    if (decoded != WrappedDeflateResult::decoded) return InspectionResult::unsupported;

    const char* layer = gzip
        ? (archive_depth ? "GZip2" : "GZip")
        : (archive_depth ? "Zlib2" : "Zlib");
    if (classify_decoded(path, output, decoded_size, result, layer)) {
        return InspectionResult::matched;
    }
    return inspect_payload(path, output, decoded_size, archive_depth + 1U,
        output, nested_alternate, result);
}

InspectionResult inspect_payload(const char* path,
                                 const uint8_t* data,
                                 uint32_t size,
                                 uint32_t archive_depth,
                                 uint8_t* scratch,
                                 uint8_t* alternate,
                                 security_guard::ScanResult& result) {
    if (!data && size) return InspectionResult::unsupported;
    if (looks_like_zip(data, size) || (archive_depth == 0U && path_has_zip_extension(path))) {
        if (scratch == data) {
            if (!alternate || alternate == data) return InspectionResult::unsupported;
            return inspect_zip(path, data, size, archive_depth, alternate, scratch, result);
        }
        if (!scratch) return InspectionResult::unsupported;
        return inspect_zip(path, data, size, archive_depth, scratch, alternate, result);
    }

    if (looks_like_gzip(data, size) ||
        (archive_depth == 0U && path_has_gzip_extension(path))) {
        return inspect_wrapped_deflate_payload(path, data, size, archive_depth,
            scratch, alternate, result, true);
    }
    if (looks_like_zlib_stream(data, size) ||
        (archive_depth == 0U && path_has_zlib_extension(path))) {
        return inspect_wrapped_deflate_payload(path, data, size, archive_depth,
            scratch, alternate, result, false);
    }

    uint32_t decoded_size = 0U;
    if (decode_base64(data, size, scratch, workspace_bytes, decoded_size) && decoded_size) {
        const InspectionResult decoded = inspect_decoded_payload(path, scratch, decoded_size,
            archive_depth, alternate, result, "Base64");
        if (decoded != InspectionResult::clean) return decoded;
    }

    const InspectionResult embedded = inspect_embedded_base64(path, data, size,
        archive_depth, scratch, alternate, result);
    if (embedded != InspectionResult::clean) return embedded;

    if (text_case_candidate(data, size)) {
        uppercase_copy(data, size, scratch);
        if (classify_decoded(path, scratch, size, result, "CaseFold")) {
            return InspectionResult::matched;
        }
    }
    return InspectionResult::clean;
}
