int32_t base64_value(uint8_t value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+' || value == '-') return 62;
    if (value == '/' || value == '_') return 63;
    return -1;
}

bool emit_base64_quartet(const uint8_t quartet[4],
                         uint32_t quartet_size,
                         uint8_t* output,
                         uint32_t output_capacity,
                         uint32_t& output_size,
                         bool terminal) {
    if (quartet_size < 2U || quartet_size > 4U || quartet[0] >= 64U || quartet[1] >= 64U) return false;
    const bool two_bytes = quartet_size >= 3U && quartet[2] < 64U;
    const bool three_bytes = quartet_size == 4U && quartet[3] < 64U;
    if (quartet_size == 4U && quartet[2] == 64U && quartet[3] != 64U) return false;
    if (quartet_size == 3U && quartet[2] == 64U) return false;
    if (quartet_size == 2U || (quartet_size == 4U && quartet[2] == 64U)) {
        if ((quartet[1] & 0x0FU) != 0U) return false;
    } else if (quartet_size == 3U || (quartet_size == 4U && quartet[3] == 64U)) {
        if ((quartet[2] & 0x03U) != 0U) return false;
    }
    if (output_size >= output_capacity) return false;
    output[output_size++] = static_cast<uint8_t>((quartet[0] << 2U) | (quartet[1] >> 4U));
    if (two_bytes) {
        if (output_size >= output_capacity) return false;
        output[output_size++] = static_cast<uint8_t>((quartet[1] << 4U) | (quartet[2] >> 2U));
    }
    if (three_bytes) {
        if (output_size >= output_capacity) return false;
        output[output_size++] = static_cast<uint8_t>((quartet[2] << 6U) | quartet[3]);
    }
    return terminal || quartet_size == 4U;
}

bool decode_base64(const uint8_t* input,
                   uint32_t input_size,
                   uint8_t* output,
                   uint32_t output_capacity,
                   uint32_t& output_size) {
    output_size = 0U;
    if ((!input && input_size) || !output || !output_capacity) return false;
    uint8_t quartet[4]{};
    uint32_t quartet_size = 0U;
    uint32_t significant = 0U;
    bool padding_started = false;
    bool terminal_quartet = false;

    for (uint32_t i = 0U; i < input_size; ++i) {
        const uint8_t value = input[i];
        if (value == ' ' || value == '\n' || value == '\r' || value == '\t') continue;
        if (terminal_quartet || quartet_size >= 4U) return false;
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
        const bool padded = quartet[2] == 64U || quartet[3] == 64U;
        if (!emit_base64_quartet(quartet, quartet_size, output, output_capacity, output_size, true)) return false;
        quartet_size = 0U;
        terminal_quartet = padded;
    }
    if (significant < 16U || quartet_size == 1U) return false;
    if (quartet_size && !emit_base64_quartet(quartet, quartet_size,
            output, output_capacity, output_size, true)) return false;
    return true;
}

bool text_case_candidate(const uint8_t* data, uint32_t size) {
    if (!data || size < 8U || size > workspace_bytes) return false;
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
