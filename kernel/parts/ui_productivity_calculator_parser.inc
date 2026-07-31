struct ProductivityCalculatorParser {
    const char* input;
    uint8_t size;
    uint8_t position;
    bool allow_bitwise;
    ProductivityCalculatorError error;
};

void productivity_calculator_skip_spaces(ProductivityCalculatorParser& parser) {
    while (parser.position < parser.size && parser.input[parser.position] == ' ') ++parser.position;
}

bool productivity_calculator_take(ProductivityCalculatorParser& parser, char value) {
    productivity_calculator_skip_spaces(parser);
    if (parser.position < parser.size && parser.input[parser.position] == value) {
        ++parser.position;
        return true;
    }
    return false;
}

bool productivity_calculator_take_two(ProductivityCalculatorParser& parser, char first, char second) {
    productivity_calculator_skip_spaces(parser);
    if (parser.position + 1U < parser.size && parser.input[parser.position] == first && parser.input[parser.position + 1U] == second) {
        parser.position = static_cast<uint8_t>(parser.position + 2U);
        return true;
    }
    return false;
}

bool productivity_calculator_checked_add(int32_t left, int32_t right, int32_t& output) {
    if ((right > 0 && left > productivity_calc_int_max - right) ||
        (right < 0 && left < productivity_calc_int_min - right)) return false;
    output = static_cast<int32_t>(left + right);
    return true;
}

bool productivity_calculator_checked_sub(int32_t left, int32_t right, int32_t& output) {
    if ((right < 0 && left > productivity_calc_int_max + right) ||
        (right > 0 && left < productivity_calc_int_min + right)) return false;
    output = static_cast<int32_t>(left - right);
    return true;
}

bool productivity_calculator_checked_mul(int32_t left, int32_t right, int32_t& output) {
    if (!left || !right) { output = 0; return true; }
    if ((left == -1 && right == productivity_calc_int_min) || (right == -1 && left == productivity_calc_int_min)) return false;
    if (left > 0) {
        if (right > 0) { if (left > productivity_calc_int_max / right) return false; }
        else if (right < productivity_calc_int_min / left) return false;
    } else {
        if (right > 0) { if (left < productivity_calc_int_min / right) return false; }
        else if (left < productivity_calc_int_max / right) return false;
    }
    output = static_cast<int32_t>(left * right);
    return true;
}

int32_t productivity_calculator_parse_or(ProductivityCalculatorParser& parser);

int32_t productivity_calculator_parse_primary(ProductivityCalculatorParser& parser) {
    productivity_calculator_skip_spaces(parser);
    if (productivity_calculator_take(parser, '(')) {
        const int32_t value = productivity_calculator_parse_or(parser);
        if (!productivity_calculator_take(parser, ')') && parser.error == ProductivityCalculatorError::none)
            parser.error = ProductivityCalculatorError::syntax;
        return value;
    }
    uint32_t base = 10U;
    if (parser.position + 1U < parser.size && parser.input[parser.position] == '0' &&
        (parser.input[parser.position + 1U] == 'x' || parser.input[parser.position + 1U] == 'X')) {
        base = 16U;
        parser.position = static_cast<uint8_t>(parser.position + 2U);
    } else if (parser.position + 1U < parser.size && parser.input[parser.position] == '0' &&
               (parser.input[parser.position + 1U] == 'b' || parser.input[parser.position + 1U] == 'B')) {
        base = 2U;
        parser.position = static_cast<uint8_t>(parser.position + 2U);
    }
    uint32_t value = 0U;
    uint8_t digits = 0U;
    while (parser.position < parser.size) {
        const char character = parser.input[parser.position];
        uint32_t digit = 99U;
        if (character >= '0' && character <= '9') digit = static_cast<uint32_t>(character - '0');
        else if (character >= 'a' && character <= 'f') digit = 10U + static_cast<uint32_t>(character - 'a');
        else if (character >= 'A' && character <= 'F') digit = 10U + static_cast<uint32_t>(character - 'A');
        if (digit >= base) break;
        if (value > (2147483647U - digit) / base) {
            parser.error = ProductivityCalculatorError::overflow;
            return 0;
        }
        value = value * base + digit;
        ++parser.position;
        ++digits;
    }
    if (!digits) parser.error = ProductivityCalculatorError::syntax;
    return static_cast<int32_t>(value);
}

int32_t productivity_calculator_parse_unary(ProductivityCalculatorParser& parser) {
    if (productivity_calculator_take(parser, '+')) return productivity_calculator_parse_unary(parser);
    if (productivity_calculator_take(parser, '-')) {
        const int32_t value = productivity_calculator_parse_unary(parser);
        if (value == productivity_calc_int_min) { parser.error = ProductivityCalculatorError::overflow; return 0; }
        return static_cast<int32_t>(-value);
    }
    if (productivity_calculator_take(parser, '~')) {
        if (!parser.allow_bitwise) { parser.error = ProductivityCalculatorError::syntax; return 0; }
        return static_cast<int32_t>(~static_cast<uint32_t>(productivity_calculator_parse_unary(parser)));
    }
    return productivity_calculator_parse_primary(parser);
}
