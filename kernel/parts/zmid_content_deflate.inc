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
        while (repeat) { --repeat; lengths[used++] = value; }
    }

    if (!lengths[256]) return false;
    return build_huffman(literals, lengths, literal_count) &&
        build_huffman(distances, lengths + literal_count, distance_count, true);
}

bool inflate_raw_prefix(const uint8_t* input,
                        uint32_t input_size,
                        uint8_t* output,
                        uint32_t output_capacity,
                        uint32_t& output_size,
                        uint32_t& input_consumed,
                        uint32_t window_limit = 32768U) {
    output_size = 0U;
    input_consumed = 0U;
    if ((!input && input_size) || !output || !output_capacity ||
        !window_limit || window_limit > 32768U) return false;
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
            if (!range_fits(reader.offset, 4U, reader.size)) return false;
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
            if (!distance || distance > output_size || distance > window_limit ||
                length > output_capacity - output_size) return false;
            for (uint32_t i = 0U; i < length; ++i) {
                output[output_size] = output[output_size - distance];
                ++output_size;
            }
        }
    }
    input_consumed = reader.offset;
    return true;
}

bool inflate_raw(const uint8_t* input,
                 uint32_t input_size,
                 uint8_t* output,
                 uint32_t output_capacity,
                 uint32_t& output_size) {
    uint32_t input_consumed = 0U;
    return inflate_raw_prefix(input, input_size, output, output_capacity,
        output_size, input_consumed) && input_consumed == input_size;
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
