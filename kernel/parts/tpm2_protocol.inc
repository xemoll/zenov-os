namespace tpm2_protocol {

constexpr uint16_t st_no_sessions = 0x8001U;
constexpr uint16_t st_sessions = 0x8002U;
constexpr uint16_t alg_sha256 = 0x000BU;
constexpr uint16_t su_clear = 0x0000U;
constexpr uint32_t rh_owner = 0x40000001U;
constexpr uint32_t rs_pw = 0x40000009U;
constexpr uint32_t cc_nv_define_space = 0x0000012AU;
constexpr uint32_t cc_nv_increment = 0x00000134U;
constexpr uint32_t cc_startup = 0x00000144U;
constexpr uint32_t cc_nv_read = 0x0000014EU;
constexpr uint32_t cc_nv_read_public = 0x00000169U;
constexpr uint32_t rc_success = 0x00000000U;
constexpr uint32_t rc_initialize = 0x00000100U;
constexpr uint32_t rc_handle = 0x0000008BU;
constexpr uint32_t rc_handle_1 = 0x0000018BU;
constexpr uint32_t nv_index = 0x015A4F53U;
constexpr uint32_t nv_attributes = 0x02060016U;
constexpr uint16_t nv_data_bytes = 8U;
constexpr uint32_t command_header_bytes = 10U;
constexpr uint32_t max_command_bytes = 1024U;
constexpr uint32_t max_response_bytes = 4096U;

struct Writer {
    uint8_t* data;
    uint32_t capacity;
    uint32_t size;
    bool okay;

    Writer(uint8_t* output, uint32_t output_capacity)
        : data(output), capacity(output_capacity), size(0U), okay(output != nullptr) {}

    bool put_u8(uint8_t value) {
        if (!okay || size >= capacity) { okay = false; return false; }
        data[size++] = value;
        return true;
    }
    bool put_u16(uint16_t value) {
        return put_u8(static_cast<uint8_t>(value >> 8U)) &&
               put_u8(static_cast<uint8_t>(value));
    }
    bool put_u32(uint32_t value) {
        return put_u8(static_cast<uint8_t>(value >> 24U)) &&
               put_u8(static_cast<uint8_t>(value >> 16U)) &&
               put_u8(static_cast<uint8_t>(value >> 8U)) &&
               put_u8(static_cast<uint8_t>(value));
    }
    bool put_bytes(const uint8_t* source, uint32_t count) {
        if (!okay || (!source && count) || count > capacity - size) {
            okay = false;
            return false;
        }
        for (uint32_t i = 0U; i < count; ++i) data[size++] = source[i];
        return true;
    }
    bool patch_u32(uint32_t offset, uint32_t value) {
        if (!okay || offset > size || size - offset < 4U) { okay = false; return false; }
        data[offset] = static_cast<uint8_t>(value >> 24U);
        data[offset + 1U] = static_cast<uint8_t>(value >> 16U);
        data[offset + 2U] = static_cast<uint8_t>(value >> 8U);
        data[offset + 3U] = static_cast<uint8_t>(value);
        return true;
    }
};

struct Reader {
    const uint8_t* data;
    uint32_t size;
    uint32_t offset;
    bool okay;

    Reader(const uint8_t* input, uint32_t input_size)
        : data(input), size(input_size), offset(0U), okay(input != nullptr) {}

    bool get_u8(uint8_t& value) {
        if (!okay || offset >= size) { okay = false; return false; }
        value = data[offset++];
        return true;
    }
    bool get_u16(uint16_t& value) {
        uint8_t high = 0U, low = 0U;
        if (!get_u8(high) || !get_u8(low)) return false;
        value = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low);
        return true;
    }
    bool get_u32(uint32_t& value) {
        uint8_t a = 0U, b = 0U, c = 0U, d = 0U;
        if (!get_u8(a) || !get_u8(b) || !get_u8(c) || !get_u8(d)) return false;
        value = (static_cast<uint32_t>(a) << 24U) |
                (static_cast<uint32_t>(b) << 16U) |
                (static_cast<uint32_t>(c) << 8U) |
                static_cast<uint32_t>(d);
        return true;
    }
    bool get_u64(uint64_t& value) {
        uint32_t high = 0U, low = 0U;
        if (!get_u32(high) || !get_u32(low)) return false;
        value = (static_cast<uint64_t>(high) << 32U) | low;
        return true;
    }
    bool skip(uint32_t count) {
        if (!okay || count > size - offset) { okay = false; return false; }
        offset += count;
        return true;
    }
};

struct ResponseHeader {
    uint16_t tag;
    uint32_t size;
    uint32_t code;
};

struct CounterPublic {
    uint32_t index;
    uint16_t name_algorithm;
    uint32_t attributes;
    uint16_t data_size;
};

enum class PublicValidation : uint8_t {
    okay,
    malformed,
    response_error,
    wrong_index,
    wrong_algorithm,
    wrong_attributes,
    policy_present,
    wrong_data_size
};

bool begin_command(Writer& writer, uint16_t tag, uint32_t command_code) {
    return writer.put_u16(tag) && writer.put_u32(0U) && writer.put_u32(command_code);
}

bool finish_command(Writer& writer, uint32_t& actual_size) {
    if (!writer.okay || writer.size < command_header_bytes || writer.size > max_command_bytes ||
        !writer.patch_u32(2U, writer.size)) {
        actual_size = 0U;
        return false;
    }
    actual_size = writer.size;
    return true;
}

bool put_empty_password_session(Writer& writer) {
    constexpr uint32_t password_session_bytes = 9U;
    return writer.put_u32(password_session_bytes) &&
           writer.put_u32(rs_pw) &&
           writer.put_u16(0U) &&
           writer.put_u8(0U) &&
           writer.put_u16(0U);
}

bool build_startup(uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    Writer writer(output, capacity);
    if (!begin_command(writer, st_no_sessions, cc_startup) || !writer.put_u16(su_clear)) return false;
    return finish_command(writer, actual_size);
}

bool build_nv_read_public(uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    Writer writer(output, capacity);
    if (!begin_command(writer, st_no_sessions, cc_nv_read_public) || !writer.put_u32(nv_index)) return false;
    return finish_command(writer, actual_size);
}

bool build_nv_define_counter(uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    Writer writer(output, capacity);
    constexpr uint16_t public_area_bytes = 14U;
    if (!begin_command(writer, st_sessions, cc_nv_define_space) ||
        !writer.put_u32(rh_owner) ||
        !put_empty_password_session(writer) ||
        !writer.put_u16(0U) ||
        !writer.put_u16(public_area_bytes) ||
        !writer.put_u32(nv_index) ||
        !writer.put_u16(alg_sha256) ||
        !writer.put_u32(nv_attributes) ||
        !writer.put_u16(0U) ||
        !writer.put_u16(nv_data_bytes)) {
        return false;
    }
    return finish_command(writer, actual_size);
}

bool build_nv_increment(uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    Writer writer(output, capacity);
    if (!begin_command(writer, st_sessions, cc_nv_increment) ||
        !writer.put_u32(rh_owner) ||
        !writer.put_u32(nv_index) ||
        !put_empty_password_session(writer)) {
        return false;
    }
    return finish_command(writer, actual_size);
}

bool build_nv_read_counter(uint8_t* output, uint32_t capacity, uint32_t& actual_size) {
    Writer writer(output, capacity);
    if (!begin_command(writer, st_sessions, cc_nv_read) ||
        !writer.put_u32(rh_owner) ||
        !writer.put_u32(nv_index) ||
        !put_empty_password_session(writer) ||
        !writer.put_u16(nv_data_bytes) ||
        !writer.put_u16(0U)) {
        return false;
    }
    return finish_command(writer, actual_size);
}

bool parse_response_header(const uint8_t* response, uint32_t available, ResponseHeader& header) {
    header = ResponseHeader{};
    if (!response || available < command_header_bytes || available > max_response_bytes) return false;
    Reader reader(response, available);
    if (!reader.get_u16(header.tag) || !reader.get_u32(header.size) || !reader.get_u32(header.code)) return false;
    if ((header.tag != st_no_sessions && header.tag != st_sessions) ||
        header.size < command_header_bytes || header.size > available || header.size > max_response_bytes) {
        return false;
    }
    return true;
}

bool response_success(const uint8_t* response, uint32_t available, uint32_t& response_code) {
    ResponseHeader header{};
    if (!parse_response_header(response, available, header)) {
        response_code = 0xFFFFFFFFU;
        return false;
    }
    response_code = header.code;
    return header.code == rc_success;
}

bool missing_nv_index_code(uint32_t code) {
    return code == rc_handle || code == rc_handle_1;
}

PublicValidation parse_counter_public(const uint8_t* response, uint32_t available,
                                      CounterPublic& output, uint32_t& response_code) {
    output = CounterPublic{};
    ResponseHeader header{};
    if (!parse_response_header(response, available, header)) {
        response_code = 0xFFFFFFFFU;
        return PublicValidation::malformed;
    }
    response_code = header.code;
    if (header.code != rc_success) return PublicValidation::response_error;
    if (header.tag != st_no_sessions) return PublicValidation::malformed;

    Reader reader(response, header.size);
    if (!reader.skip(command_header_bytes)) return PublicValidation::malformed;
    uint16_t public_size = 0U;
    uint16_t policy_size = 0U;
    if (!reader.get_u16(public_size) || public_size < 14U || public_size > reader.size - reader.offset) {
        return PublicValidation::malformed;
    }
    const uint32_t public_end = reader.offset + public_size;
    if (!reader.get_u32(output.index) ||
        !reader.get_u16(output.name_algorithm) ||
        !reader.get_u32(output.attributes) ||
        !reader.get_u16(policy_size) ||
        policy_size > public_end - reader.offset ||
        !reader.skip(policy_size) ||
        !reader.get_u16(output.data_size) ||
        reader.offset != public_end) {
        return PublicValidation::malformed;
    }
    uint16_t name_size = 0U;
    if (!reader.get_u16(name_size) || name_size > reader.size - reader.offset ||
        !reader.skip(name_size) || reader.offset != header.size) {
        return PublicValidation::malformed;
    }
    if (output.index != nv_index) return PublicValidation::wrong_index;
    if (output.name_algorithm != alg_sha256) return PublicValidation::wrong_algorithm;
    if (output.attributes != nv_attributes) return PublicValidation::wrong_attributes;
    if (policy_size != 0U) return PublicValidation::policy_present;
    if (output.data_size != nv_data_bytes) return PublicValidation::wrong_data_size;
    return PublicValidation::okay;
}

bool parse_counter_value(const uint8_t* response, uint32_t available,
                         uint64_t& counter, uint32_t& response_code) {
    counter = 0U;
    ResponseHeader header{};
    if (!parse_response_header(response, available, header)) {
        response_code = 0xFFFFFFFFU;
        return false;
    }
    response_code = header.code;
    if (header.code != rc_success || header.tag != st_sessions) return false;

    Reader reader(response, header.size);
    if (!reader.skip(command_header_bytes)) return false;
    uint32_t parameter_size = 0U;
    uint16_t data_size = 0U;
    if (!reader.get_u32(parameter_size) || parameter_size != 2U + nv_data_bytes ||
        parameter_size > reader.size - reader.offset ||
        !reader.get_u16(data_size) || data_size != nv_data_bytes ||
        !reader.get_u64(counter)) {
        return false;
    }
    const uint32_t parameter_end = command_header_bytes + 4U + parameter_size;
    if (reader.offset != parameter_end || parameter_end > header.size) return false;
    return true;
}

const char* public_validation_name(PublicValidation validation) {
    switch (validation) {
        case PublicValidation::okay: return "okay";
        case PublicValidation::response_error: return "response-error";
        case PublicValidation::wrong_index: return "wrong-index";
        case PublicValidation::wrong_algorithm: return "wrong-algorithm";
        case PublicValidation::wrong_attributes: return "wrong-attributes";
        case PublicValidation::policy_present: return "policy-present";
        case PublicValidation::wrong_data_size: return "wrong-data-size";
        default: return "malformed";
    }
}

} // namespace tpm2_protocol
