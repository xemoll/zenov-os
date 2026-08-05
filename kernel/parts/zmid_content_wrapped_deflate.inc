enum class WrappedDeflateResult : uint8_t { not_format, decoded, unsupported };

bool wrapped_ascii_equal_ci(char left, char right) {
    if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
    return left == right;
}

bool wrapped_path_has_suffix(const char* path, const char* suffix) {
    if (!path || !suffix) return false;
    constexpr uint32_t max_path_scan = 1024U;
    uint32_t path_size = 0U;
    while (path_size < max_path_scan && path[path_size]) ++path_size;
    if (path_size == max_path_scan) return false;
    uint32_t suffix_size = 0U;
    while (suffix[suffix_size]) ++suffix_size;
    if (!suffix_size || suffix_size > path_size) return false;
    const uint32_t start = path_size - suffix_size;
    for (uint32_t index = 0U; index < suffix_size; ++index) {
        if (!wrapped_ascii_equal_ci(path[start + index], suffix[index])) return false;
    }
    return true;
}

bool path_has_gzip_extension(const char* path) {
    return wrapped_path_has_suffix(path, ".gz") ||
        wrapped_path_has_suffix(path, ".gzip");
}

bool path_has_zlib_extension(const char* path) {
    return wrapped_path_has_suffix(path, ".zlib") ||
        wrapped_path_has_suffix(path, ".zz");
}

bool looks_like_gzip(const uint8_t* data, uint32_t size) {
    return data && size >= 2U && data[0] == 0x1FU && data[1] == 0x8BU;
}

bool zlib_header_valid(const uint8_t* data, uint32_t size) {
    if (!data || size < 2U) return false;
    const uint8_t cmf = data[0];
    const uint8_t flg = data[1];
    if ((cmf & 0x0FU) != 8U || (cmf >> 4U) > 7U) return false;
    return (static_cast<uint32_t>(cmf) * 256U + flg) % 31U == 0U;
}

uint32_t zlib_window_limit(const uint8_t* data) {
    return 1U << (static_cast<uint32_t>(data[0] >> 4U) + 8U);
}

uint32_t load_be32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24U) |
        (static_cast<uint32_t>(data[1]) << 16U) |
        (static_cast<uint32_t>(data[2]) << 8U) |
        static_cast<uint32_t>(data[3]);
}

uint32_t adler32(const uint8_t* data, uint32_t size) {
    constexpr uint32_t modulus = 65521U;
    uint32_t first = 1U;
    uint32_t second = 0U;
    for (uint32_t index = 0U; index < size; ++index) {
        first += data[index];
        if (first >= modulus) first -= modulus;
        second += first;
        if (second >= modulus) second %= modulus;
    }
    return (second << 16U) | first;
}

bool skip_zero_terminated_field(const uint8_t* data,
                                uint32_t size,
                                uint32_t& cursor) {
    while (cursor < size) {
        if (data[cursor++] == 0U) return true;
    }
    return false;
}

WrappedDeflateResult decode_gzip(const uint8_t* input,
                                 uint32_t input_size,
                                 uint8_t* output,
                                 uint32_t output_capacity,
                                 uint32_t& output_size) {
    output_size = 0U;
    if (!looks_like_gzip(input, input_size)) return WrappedDeflateResult::not_format;
    if (!output || !output_capacity) return WrappedDeflateResult::unsupported;

    uint32_t cursor = 0U;
    uint32_t members = 0U;
    while (cursor < input_size) {
        if (++members > max_archive_entries ||
            !range_fits(cursor, 10U, input_size) ||
            input[cursor] != 0x1FU || input[cursor + 1U] != 0x8BU ||
            input[cursor + 2U] != 8U) {
            return WrappedDeflateResult::unsupported;
        }
        const uint32_t member_start = cursor;
        const uint8_t flags = input[cursor + 3U];
        if (flags & 0xE0U) return WrappedDeflateResult::unsupported;
        cursor += 10U;

        if (flags & 0x04U) {
            if (!range_fits(cursor, 2U, input_size)) return WrappedDeflateResult::unsupported;
            const uint16_t extra_size = load_le16(input + cursor);
            cursor += 2U;
            if (!range_fits(cursor, extra_size, input_size)) return WrappedDeflateResult::unsupported;
            cursor += extra_size;
        }
        if ((flags & 0x08U) && !skip_zero_terminated_field(input, input_size, cursor)) {
            return WrappedDeflateResult::unsupported;
        }
        if ((flags & 0x10U) && !skip_zero_terminated_field(input, input_size, cursor)) {
            return WrappedDeflateResult::unsupported;
        }
        if (flags & 0x02U) {
            if (!range_fits(cursor, 2U, input_size)) return WrappedDeflateResult::unsupported;
            const uint16_t expected_header_crc = load_le16(input + cursor);
            const uint16_t actual_header_crc = static_cast<uint16_t>(
                crc32(input + member_start, cursor - member_start));
            if (expected_header_crc != actual_header_crc) return WrappedDeflateResult::unsupported;
            cursor += 2U;
        }

        if (output_size >= output_capacity || cursor >= input_size) {
            return WrappedDeflateResult::unsupported;
        }
        uint32_t member_size = 0U;
        uint32_t compressed_size = 0U;
        if (!inflate_raw_prefix(input + cursor, input_size - cursor,
                output + output_size, output_capacity - output_size,
                member_size, compressed_size) || !compressed_size) {
            return WrappedDeflateResult::unsupported;
        }
        if (member_size > max_expanded_total - output_size ||
            !expansion_within_limit(compressed_size, member_size) ||
            compressed_size > input_size - cursor) {
            return WrappedDeflateResult::unsupported;
        }
        const uint32_t footer = cursor + compressed_size;
        if (!range_fits(footer, 8U, input_size)) return WrappedDeflateResult::unsupported;
        const uint32_t expected_crc = load_le32(input + footer);
        const uint32_t expected_size = load_le32(input + footer + 4U);
        if (crc32(output + output_size, member_size) != expected_crc ||
            member_size != expected_size) {
            return WrappedDeflateResult::unsupported;
        }
        output_size += member_size;
        cursor = footer + 8U;
    }
    return members ? WrappedDeflateResult::decoded : WrappedDeflateResult::unsupported;
}

WrappedDeflateResult decode_zlib(const uint8_t* input,
                                 uint32_t input_size,
                                 uint8_t* output,
                                 uint32_t output_capacity,
                                 uint32_t& output_size) {
    output_size = 0U;
    if (!zlib_header_valid(input, input_size)) return WrappedDeflateResult::not_format;
    if (!output || !output_capacity || input_size < 6U || (input[1] & 0x20U)) {
        return WrappedDeflateResult::unsupported;
    }

    uint32_t compressed_size = 0U;
    const uint32_t window_limit = zlib_window_limit(input);
    if (!inflate_raw_prefix(input + 2U, input_size - 2U, output,
            output_capacity, output_size, compressed_size, window_limit) ||
        !compressed_size || output_size > max_expanded_total ||
        !expansion_within_limit(compressed_size, output_size) ||
        compressed_size > input_size - 2U) {
        return WrappedDeflateResult::unsupported;
    }
    const uint32_t footer = 2U + compressed_size;
    if (!range_fits(footer, 4U, input_size) || footer + 4U != input_size ||
        adler32(output, output_size) != load_be32(input + footer)) {
        return WrappedDeflateResult::unsupported;
    }
    return WrappedDeflateResult::decoded;
}
