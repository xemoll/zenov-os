template <decltype(sizeof(0)) N>
decltype(sizeof(0)) string_length(const char (&text)[N]) {
    decltype(sizeof(0)) length = 0U;
    while (length < N && text[length]) ++length;
    return length;
}

namespace security_policy_format {
constexpr unsigned int u32_max = 0xFFFFFFFFU;

bool parse_nonzero_decimal_u32(const unsigned char* data, unsigned int size, unsigned int& output) {
    output = 0U;
    if (!data || size < 2U || size > 11U || data[size - 1U] != '\n') return false;
    const unsigned int digits = size - 1U;
    if (!digits || (digits > 1U && data[0] == '0')) return false;
    unsigned int value = 0U;
    for (unsigned int i = 0U; i < digits; ++i) {
        if (data[i] < '0' || data[i] > '9') return false;
        const unsigned int digit = static_cast<unsigned int>(data[i] - '0');
        if (value > (u32_max - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    if (!value) return false;
    output = value;
    return true;
}

bool next_version(unsigned int current, unsigned int& output) {
    if (current == u32_max) { output = 0U; return false; }
    output = current + 1U;
    return true;
}

bool canonical_absolute_path_with_length(const char* value, unsigned int capacity,
                                         unsigned int declared_length, bool allow_empty = false) {
    if (!value || !capacity || declared_length >= capacity) return false;
    if (value[declared_length] != 0) return false;
    for (unsigned int i = 0U; i < declared_length; ++i) if (value[i] == 0) return false;
    for (unsigned int i = declared_length + 1U; i < capacity; ++i) if (value[i] != 0) return false;
    if (!declared_length) return allow_empty;
    if (value[0] != '/' || declared_length < 2U || value[declared_length - 1U] == '/') return false;

    unsigned int component_start = 1U;
    for (unsigned int i = 1U; i <= declared_length; ++i) {
        if (i < declared_length) {
            const unsigned char byte = static_cast<unsigned char>(value[i]);
            if (byte < 0x20U || byte > 0x7EU || value[i] == '\\') return false;
            if (value[i] == '/' && value[i - 1U] == '/') return false;
        }
        if (i == declared_length || value[i] == '/') {
            const unsigned int component_length = i - component_start;
            if (!component_length ||
                (component_length == 1U && value[component_start] == '.') ||
                (component_length == 2U && value[component_start] == '.' && value[component_start + 1U] == '.')) return false;
            component_start = i + 1U;
        }
    }
    return true;
}

bool canonical_absolute_path(const char* value, unsigned int capacity, bool allow_empty = false) {
    if (!value || !capacity) return false;
    unsigned int length = 0U;
    while (length < capacity && value[length]) ++length;
    if (length == capacity) return false;
    return canonical_absolute_path_with_length(value, capacity, length, allow_empty);
}
} // namespace security_policy_format
