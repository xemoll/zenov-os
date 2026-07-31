namespace zmid {
namespace content_inspection {

constexpr uint32_t workspace_bytes = 64U * 1024U;
constexpr uint32_t cache_capacity = 16U;
constexpr uint32_t max_archive_depth = 2U;
constexpr uint32_t max_archive_entries = 16U;
constexpr uint32_t max_expanded_total = 64U * 1024U;
constexpr uint32_t max_expansion_ratio = 128U;
constexpr uint32_t max_deflate_blocks = 128U;

uint8_t* workspace_a = nullptr;
uint8_t* workspace_b = nullptr;
uint32_t cache_next = 0U;
uint32_t raw_hits = 0U;
uint32_t transformed_hits = 0U;
uint32_t cache_hits = 0U;
uint32_t unsupported_containers = 0U;

struct CacheEntry {
    bool valid = false;
    bool matched = false;
    uint32_t database_version = 0U;
    uint8_t digest[security_guard::sha256_bytes]{};
    security_guard::Verdict verdict = security_guard::Verdict::error;
    uint8_t action = 0U;
    char signature[48]{};
};

CacheEntry cache[cache_capacity]{};

uint16_t load_le16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t load_le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8U) |
        (static_cast<uint32_t>(data[2]) << 16U) |
        (static_cast<uint32_t>(data[3]) << 24U);
}

bool ensure_workspace() {
    if (workspace_a && workspace_b) return true;
    workspace_a = static_cast<uint8_t*>(heap::allocate(workspace_bytes, 16U));
    workspace_b = static_cast<uint8_t*>(heap::allocate(workspace_bytes, 16U));
    return workspace_a && workspace_b;
}

void clear_cache() {
    for (auto& entry : cache) entry = CacheEntry{};
    cache_next = 0U;
}

void compose_signature(char output[48], const char* layer, const char* signature) {
    uint32_t used = 0U;
    auto append = [&](const char* text) {
        while (text && *text && used + 1U < 48U) output[used++] = *text++;
    };
    append(layer);
    append(":");
    append(signature && *signature ? signature : "Matched");
    output[used] = 0;
}

void copy_match(security_guard::ScanResult& output,
                const security_guard::ScanResult& inner,
                const char* layer) {
    output.verdict = inner.verdict;
    output.policy_action = inner.policy_action;
    compose_signature(output.signature, layer, inner.signature);
}

bool classify_decoded(const char* path,
                      const uint8_t* data,
                      uint32_t size,
                      security_guard::ScanResult& output,
                      const char* layer) {
    uint8_t digest[security_guard::sha256_bytes]{};
    security_guard::sha256(data, size, digest);
    security_guard::ScanResult inner{};
    if (!classify_raw(path, data, size, digest, inner)) return false;
    copy_match(output, inner, layer);
    return true;
}

struct BitReader {
    const uint8_t* data = nullptr;
    uint32_t size = 0U;
    uint32_t offset = 0U;
    uint32_t held = 0U;
    uint32_t bit_count = 0U;

    bool read(uint32_t bits, uint32_t& output) {
        if (!bits || bits > 16U) return false;
        while (bit_count < bits) {
            if (offset >= size) return false;
            held |= static_cast<uint32_t>(data[offset++]) << bit_count;
            bit_count += 8U;
        }
        output = held & ((1U << bits) - 1U);
        held >>= bits;
        bit_count -= bits;
        return true;
    }

    void align_byte() {
        held = 0U;
        bit_count = 0U;
    }
};

struct HuffmanTable {
    uint16_t count[16]{};
    uint16_t symbols[288]{};
};

bool build_huffman(HuffmanTable& table,
                   const uint8_t* lengths,
                   uint32_t symbol_count,
                   bool allow_empty = false) {
    if (!lengths || symbol_count > 288U) return false;
    table = HuffmanTable{};
    for (uint32_t symbol = 0U; symbol < symbol_count; ++symbol) {
        if (lengths[symbol] > 15U) return false;
        ++table.count[lengths[symbol]];
    }
    if (table.count[0] == symbol_count) return allow_empty;

    int32_t remaining = 1;
    for (uint32_t length = 1U; length <= 15U; ++length) {
        remaining <<= 1;
        remaining -= table.count[length];
        if (remaining < 0) return false;
    }

    uint16_t offsets[16]{};
    for (uint32_t length = 1U; length < 15U; ++length) {
        offsets[length + 1U] = static_cast<uint16_t>(offsets[length] + table.count[length]);
    }
    for (uint32_t symbol = 0U; symbol < symbol_count; ++symbol) {
        const uint8_t length = lengths[symbol];
        if (length) table.symbols[offsets[length]++] = static_cast<uint16_t>(symbol);
    }
    return true;
}

bool decode_symbol(BitReader& reader, const HuffmanTable& table, uint32_t& symbol) {
    uint32_t code = 0U;
    uint32_t first = 0U;
    uint32_t index = 0U;
    for (uint32_t length = 1U; length <= 15U; ++length) {
        uint32_t bit = 0U;
        if (!reader.read(1U, bit)) return false;
        code |= bit;
        const uint32_t count = table.count[length];
        if (code >= first && code - first < count) {
            symbol = table.symbols[index + code - first];
            return true;
        }
        index += count;
        first = (first + count) << 1U;
        code <<= 1U;
    }
    return false;
}

static constexpr uint16_t length_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,
    31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static constexpr uint8_t length_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,
    2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static constexpr uint16_t distance_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,
    193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577
};
static constexpr uint8_t distance_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,
    6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

bool build_fixed_tables(HuffmanTable& literals, HuffmanTable& distances) {
    uint8_t literal_lengths[288]{};
    for (uint32_t i = 0U; i <= 143U; ++i) literal_lengths[i] = 8U;
    for (uint32_t i = 144U; i <= 255U; ++i) literal_lengths[i] = 9U;
    for (uint32_t i = 256U; i <= 279U; ++i) literal_lengths[i] = 7U;
    for (uint32_t i = 280U; i <= 287U; ++i) literal_lengths[i] = 8U;
    uint8_t distance_lengths[32]{};
    for (auto& length : distance_lengths) length = 5U;
    return build_huffman(literals, literal_lengths, 288U) &&
        build_huffman(distances, distance_lengths, 32U);
}

bool build_dynamic_tables(BitReader& reader,
                          HuffmanTable& literals,
                          HuffmanTable& distances) {
    uint32_t literal_bits = 0U, distance_bits = 0U, code_bits = 0U;
    if (!reader.read(5U, literal_bits) || !reader.read(5U, distance_bits) ||
        !reader.read(4U, code_bits)) return false;
    const uint32_t literal_count = literal_bits + 257U;
    const uint32_t distance_count = distance_bits + 1U;
    const uint32_t code_count = code_bits + 4U;
    if (literal_count > 286U || distance_count > 32U || code_count > 19U) return false;

    static constexpr uint8_t code_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };
    uint8_t code_lengths[19]{};
    for (uint32_t i = 0U; i < code_count; ++i) {
        uint32_t value = 0U;
        if (!reader.read(3U, value)) return false;
        code_lengths[code_order[i]] = static_cast<uint8_t>(value);
    }

    HuffmanTable code_table{};
    if (!build_huffman(code_table, code_lengths, 19U)) return false;

    uint8_t lengths[320]{};
    const uint32_t total = literal_count + distance_count;
    uint32_t used = 0U;
    while (used < total) {
        uint32_t symbol = 0U;
        if (!decode_symbol(reader, code_table, symbol)) return false;
        if (symbol <= 15U) {
            lengths[used++] = static_cast<uint8_t>(symbol);
            continue;
        }

        uint32_t repeat_bits = 0U;
        uint32_t repeat = 0U;
        uint8_t value = 0U;
        if (symbol == 16U) {
            if (!used || !reader.read(2U, repeat_bits)) return false;
            repeat = repeat_bits + 3U;
            value = lengths[used - 1U];
        } else if (symbol == 17U) {
            if (!reader.read(3U, repeat_bits)) return false;
            repeat = repeat_bits + 3U;
        } else if (symbol == 18U) {
            if (!reader.read(7U, repeat_bits)) return false;
            repeat = repeat_bits + 11U;
        } else return false;
        if (repeat > total - used) return false;
        while (repeat--) lengths[used++] = value;
    }

    if (!lengths[256]) return false;
    return build_huffman(literals, lengths, literal_count) &&
        build_huffman(distances, lengths + literal_count, distance_count, true);
}

bool inflate_raw(const uint8_t* input,
                 uint32_t input_size,
                 uint8_t* output,
                 uint32_t output_capacity,
                 uint32_t& output_size) {
    output_size = 0U;
    if ((!input && input_size) || !output || !output_capacity) return false;
    BitReader reader{input, input_size};
    bool final_block = false;
    uint32_t blocks = 0U;
    while (!final_block) {
        if (++blocks > max_deflate_blocks) return false;
        uint32_t final_value = 0U, block_type = 0U;
        if (!reader.read(1U, final_value) || !reader.read(2U, block_type)) return false;
        final_block = final_value != 0U;

        if (block_type == 0U) {
            reader.align_byte();
            if (reader.offset + 4U > reader.size) return false;
            const uint16_t length = load_le16(reader.data + reader.offset);
            const uint16_t inverse = load_le16(reader.data + reader.offset + 2U);
            reader.offset += 4U;
            if (static_cast<uint16_t>(length ^ 0xFFFFU) != inverse ||
                length > reader.size - reader.offset ||
                length > output_capacity - output_size) return false;
            memcpy(output + output_size, reader.data + reader.offset, length);
            output_size += length;
            reader.offset += length;
            continue;
        }
        if (block_type == 3U) return false;

        HuffmanTable literals{}, distances{};
        if (block_type == 1U) {
            if (!build_fixed_tables(literals, distances)) return false;
        } else if (!build_dynamic_tables(reader, literals, distances)) return false;

        uint32_t operations = 0U;
        for (;;) {
            if (++operations > output_capacity * 2U + 1024U) return false;
            uint32_t symbol = 0U;
            if (!decode_symbol(reader, literals, symbol)) return false;
            if (symbol < 256U) {
                if (output_size >= output_capacity) return false;
                output[output_size++] = static_cast<uint8_t>(symbol);
                continue;
            }
            if (symbol == 256U) break;
            if (symbol < 257U || symbol > 285U) return false;

            const uint32_t length_index = symbol - 257U;
            uint32_t extra = 0U;
            if (length_extra[length_index] &&
                !reader.read(length_extra[length_index], extra)) return false;
            const uint32_t length = length_base[length_index] + extra;

            uint32_t distance_symbol = 0U;
            if (!decode_symbol(reader, distances, distance_symbol) || distance_symbol > 29U) return false;
            extra = 0U;
            if (distance_extra[distance_symbol] &&
                !reader.read(distance_extra[distance_symbol], extra)) return false;
            const uint32_t distance = distance_base[distance_symbol] + extra;
            if (!distance || distance > output_size || length > output_capacity - output_size) return false;
            for (uint32_t i = 0U; i < length; ++i) {
                output[output_size] = output[output_size - distance];
                ++output_size;
            }
        }
    }
    return true;
}

uint32_t crc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

int32_t base64_value(uint8_t value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool decode_base64(const uint8_t* input,
                   uint32_t input_size,
                   uint8_t* output,
                   uint32_t output_capacity,
                   uint32_t& output_size) {
    output_size = 0U;
    uint8_t quartet[4]{};
    uint32_t quartet_size = 0U;
    uint32_t significant = 0U;
    bool padding_started = false;
    bool terminal_quartet = false;

    for (uint32_t i = 0U; i < input_size; ++i) {
        const uint8_t value = input[i];
        if (value == ' ' || value == '\n' || value == '\r' || value == '\t') continue;
        if (terminal_quartet) return false;
        if (value == '=') {
            padding_started = true;
            quartet[quartet_size++] = 64U;
        } else {
            const int32_t decoded = base64_value(value);
            if (decoded < 0 || padding_started) return false;
            quartet[quartet_size++] = static_cast<uint8_t>(decoded);
        }
        ++significant;
        if (quartet_size != 4U) continue;
        if (quartet[0] >= 64U || quartet[1] >= 64U || output_size >= output_capacity) return false;
        output[output_size++] = static_cast<uint8_t>((quartet[0] << 2U) | (quartet[1] >> 4U));
        if (quartet[2] == 64U) {
            if (quartet[3] != 64U) return false;
            terminal_quartet = true;
        } else {
            if (output_size >= output_capacity) return false;
            output[output_size++] = static_cast<uint8_t>((quartet[1] << 4U) | (quartet[2] >> 2U));
            if (quartet[3] == 64U) terminal_quartet = true;
            else {
                if (output_size >= output_capacity) return false;
                output[output_size++] = static_cast<uint8_t>((quartet[2] << 6U) | quartet[3]);
            }
        }
        quartet_size = 0U;
    }
    return quartet_size == 0U && significant >= 16U && (significant & 3U) == 0U;
}

bool text_case_candidate(const uint8_t* data, uint32_t size) {
    if (!data || size < 8U) return false;
    uint32_t printable = 0U;
    uint32_t lowercase = 0U;
    for (uint32_t i = 0U; i < size; ++i) {
        const uint8_t value = data[i];
        if ((value >= 32U && value <= 126U) || value == '\n' || value == '\r' || value == '\t') ++printable;
        if (value >= 'a' && value <= 'z') ++lowercase;
    }
    return lowercase && printable * 10U >= size * 9U;
}

void uppercase_copy(const uint8_t* input, uint32_t size, uint8_t* output) {
    for (uint32_t i = 0U; i < size; ++i) {
        const uint8_t value = input[i];
        output[i] = (value >= 'a' && value <= 'z')
            ? static_cast<uint8_t>(value - 'a' + 'A') : value;
    }
}

enum class InspectionResult : uint8_t { clean, matched, unsupported };

InspectionResult inspect_payload(const char* path,
                                 const uint8_t* data,
                                 uint32_t size,
                                 uint32_t archive_depth,
                                 uint8_t* scratch,
                                 uint8_t* alternate,
                                 security_guard::ScanResult& result);

InspectionResult inspect_zip(const char* path,
                             const uint8_t* data,
                             uint32_t size,
                             uint32_t archive_depth,
                             uint8_t* output,
                             uint8_t* alternate,
                             security_guard::ScanResult& result) {
    if (archive_depth >= max_archive_depth) return InspectionResult::unsupported;
    uint32_t offset = 0U;
    uint32_t entries = 0U;
    uint32_t total_expanded = 0U;
    bool found_entry = false;

    while (offset + 4U <= size) {
        const uint32_t signature = load_le32(data + offset);
        if (signature == 0x02014B50U || signature == 0x06054B50U) break;
        if (signature != 0x04034B50U || offset + 30U > size || ++entries > max_archive_entries) {
            return InspectionResult::unsupported;
        }

        const uint16_t flags = load_le16(data + offset + 6U);
        const uint16_t method = load_le16(data + offset + 8U);
        const uint32_t expected_crc = load_le32(data + offset + 14U);
        const uint32_t compressed_size = load_le32(data + offset + 18U);
        const uint32_t expanded_size = load_le32(data + offset + 22U);
        const uint16_t name_size = load_le16(data + offset + 26U);
        const uint16_t extra_size = load_le16(data + offset + 28U);

        if ((flags & 0x0009U) || expanded_size > workspace_bytes ||
            offset + 30U + name_size + extra_size > size) return InspectionResult::unsupported;
        const uint32_t payload_offset = offset + 30U + name_size + extra_size;
        if (compressed_size > size - payload_offset ||
            expanded_size > max_expanded_total - total_expanded) return InspectionResult::unsupported;
        if (compressed_size && expanded_size > compressed_size * max_expansion_ratio + 1024U) {
            return InspectionResult::unsupported;
        }

        uint32_t actual_size = 0U;
        if (method == 0U) {
            if (compressed_size != expanded_size) return InspectionResult::unsupported;
            memcpy(output, data + payload_offset, expanded_size);
            actual_size = expanded_size;
        } else if (method == 8U) {
            if (!inflate_raw(data + payload_offset, compressed_size,
                    output, workspace_bytes, actual_size) || actual_size != expanded_size) {
                return InspectionResult::unsupported;
            }
        } else return InspectionResult::unsupported;

        if (crc32(output, actual_size) != expected_crc) return InspectionResult::unsupported;
        total_expanded += actual_size;
        found_entry = true;

        if (classify_decoded(path, output, actual_size, result,
                archive_depth ? "ZIP2" : "ZIP")) return InspectionResult::matched;
        const InspectionResult nested = inspect_payload(path, output, actual_size,
            archive_depth + 1U, alternate, output, result);
        if (nested != InspectionResult::clean) return nested;
        offset = payload_offset + compressed_size;
    }
    return found_entry ? InspectionResult::clean : InspectionResult::unsupported;
}

InspectionResult inspect_payload(const char* path,
                                 const uint8_t* data,
                                 uint32_t size,
                                 uint32_t archive_depth,
                                 uint8_t* scratch,
                                 uint8_t* alternate,
                                 security_guard::ScanResult& result) {
    if (!data && size) return InspectionResult::unsupported;
    if (size >= 4U && load_le32(data) == 0x04034B50U) {
        return inspect_zip(path, data, size, archive_depth, scratch, alternate, result);
    }

    uint32_t decoded_size = 0U;
    if (decode_base64(data, size, scratch, workspace_bytes, decoded_size) && decoded_size) {
        if (classify_decoded(path, scratch, decoded_size, result, "Base64")) {
            return InspectionResult::matched;
        }
        if (decoded_size >= 4U && load_le32(scratch) == 0x04034B50U) {
            const InspectionResult archive = inspect_zip(path, scratch, decoded_size,
                archive_depth + 1U, alternate, scratch, result);
            if (archive != InspectionResult::clean) return archive;
        }
        if (text_case_candidate(scratch, decoded_size)) {
            uppercase_copy(scratch, decoded_size, alternate);
            if (classify_decoded(path, alternate, decoded_size, result, "Base64Case")) {
                return InspectionResult::matched;
            }
        }
    }

    if (text_case_candidate(data, size)) {
        uppercase_copy(data, size, scratch);
        if (classify_decoded(path, scratch, size, result, "CaseFold")) {
            return InspectionResult::matched;
        }
    }
    return InspectionResult::clean;
}

bool cache_lookup(const uint8_t digest[security_guard::sha256_bytes],
                  security_guard::ScanResult& result,
                  bool& matched) {
    for (const auto& entry : cache) {
        if (!entry.valid || entry.database_version != active_database_version ||
            !security_guard::digest_equal(entry.digest, digest)) continue;
        ++cache_hits;
        matched = entry.matched;
        if (matched) {
            result.verdict = entry.verdict;
            result.policy_action = entry.action;
            storage::path_copy(result.signature, entry.signature, sizeof(result.signature));
        }
        return true;
    }
    return false;
}

void cache_store(const uint8_t digest[security_guard::sha256_bytes],
                 bool matched,
                 const security_guard::ScanResult& result) {
    CacheEntry& entry = cache[cache_next++ % cache_capacity];
    entry = CacheEntry{};
    entry.valid = true;
    entry.matched = matched;
    entry.database_version = active_database_version;
    memcpy(entry.digest, digest, sizeof(entry.digest));
    if (matched) {
        entry.verdict = result.verdict;
        entry.action = result.policy_action;
        storage::path_copy(entry.signature, result.signature, sizeof(entry.signature));
    }
}

bool self_test() {
    if (!ensure_workspace()) return false;
    static constexpr uint8_t case_vector[] = "zenov_ransomware_test_v1";
    static constexpr uint8_t base64_vector[] = "WkVOT1ZfUkFOU09NV0FSRV9URVNUX1Yx";
    static constexpr uint8_t deflate_vector[] = {
        0x8B,0x72,0xF5,0xF3,0x0F,0x8B,0x0F,0x72,0xF4,0x0B,0xF6,0xF7,0x0D,
        0x77,0x0C,0x72,0x8D,0x0F,0x71,0x0D,0x0E,0x89,0x0F,0x33,0x04,0x00
    };
    static constexpr uint8_t expected[] = "ZENOV_RANSOMWARE_TEST_V1";

    security_guard::ScanResult result{};
    if (inspect_payload("<content-case-selftest>", case_vector, sizeof(case_vector) - 1U,
            0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) return false;
    result = {};
    if (inspect_payload("<content-base64-selftest>", base64_vector, sizeof(base64_vector) - 1U,
            0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) return false;

    uint32_t expanded = 0U;
    if (!inflate_raw(deflate_vector, sizeof(deflate_vector), workspace_a,
            workspace_bytes, expanded) || expanded != sizeof(expected) - 1U ||
        !storage::bytes_equal(workspace_a, expected, expanded)) return false;
    clear_cache();
    return true;
}

bool init() {
    if (!self_test()) return false;
    serial::line("ZMID_CONTENT_INSPECTION_OK base64=1 casefold=1 zip=1 deflate=1 depth=2 entries=16 expanded=65536 cache=16");
    return true;
}

} // namespace content_inspection

bool classify(const char* path,
              const uint8_t* data,
              uint32_t size,
              const uint8_t digest[security_guard::sha256_bytes],
              security_guard::ScanResult& result) {
    if (classify_raw(path, data, size, digest, result)) {
        ++content_inspection::raw_hits;
        return true;
    }
    if (!ready || (!data && size) || !digest) return false;

    bool matched = false;
    if (content_inspection::cache_lookup(digest, result, matched)) return matched;
    if (!content_inspection::ensure_workspace()) {
        result.verdict = security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, "Inspection.Workspace", sizeof(result.signature));
        return true;
    }

    const content_inspection::InspectionResult inspection = content_inspection::inspect_payload(
        path, data, size, 0U, content_inspection::workspace_a,
        content_inspection::workspace_b, result);
    if (inspection == content_inspection::InspectionResult::unsupported) {
        ++content_inspection::unsupported_containers;
        result.verdict = security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, "Container.Unsupported", sizeof(result.signature));
        matched = true;
    } else {
        matched = inspection == content_inspection::InspectionResult::matched;
        if (matched) ++content_inspection::transformed_hits;
    }
    content_inspection::cache_store(digest, matched, result);
    return matched;
}

} // namespace zmid
