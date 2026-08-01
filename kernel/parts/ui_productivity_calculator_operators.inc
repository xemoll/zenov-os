int32_t productivity_calculator_parse_mul(ProductivityCalculatorParser& parser) {
    int32_t value = productivity_calculator_parse_unary(parser);
    while (parser.error == ProductivityCalculatorError::none) {
        if (productivity_calculator_take(parser, '*')) {
            int32_t result = 0;
            if (!productivity_calculator_checked_mul(value, productivity_calculator_parse_unary(parser), result))
                parser.error = ProductivityCalculatorError::overflow;
            value = result;
        } else if (productivity_calculator_take(parser, '/')) {
            const int32_t right = productivity_calculator_parse_unary(parser);
            if (!right) parser.error = ProductivityCalculatorError::divide_by_zero;
            else if (value == productivity_calc_int_min && right == -1) parser.error = ProductivityCalculatorError::overflow;
            else value = static_cast<int32_t>(value / right);
        } else if (productivity_calculator_take(parser, '%')) {
            const int32_t right = productivity_calculator_parse_unary(parser);
            if (!right) parser.error = ProductivityCalculatorError::divide_by_zero;
            else if (value == productivity_calc_int_min && right == -1) value = 0;
            else value = static_cast<int32_t>(value % right);
        } else break;
    }
    return value;
}

int32_t productivity_calculator_parse_add(ProductivityCalculatorParser& parser) {
    int32_t value = productivity_calculator_parse_mul(parser);
    while (parser.error == ProductivityCalculatorError::none) {
        if (productivity_calculator_take(parser, '+')) {
            int32_t result = 0;
            if (!productivity_calculator_checked_add(value, productivity_calculator_parse_mul(parser), result))
                parser.error = ProductivityCalculatorError::overflow;
            value = result;
        } else if (productivity_calculator_take(parser, '-')) {
            int32_t result = 0;
            if (!productivity_calculator_checked_sub(value, productivity_calculator_parse_mul(parser), result))
                parser.error = ProductivityCalculatorError::overflow;
            value = result;
        } else break;
    }
    return value;
}

int32_t productivity_calculator_parse_shift(ProductivityCalculatorParser& parser) {
    int32_t value = productivity_calculator_parse_add(parser);
    while (parser.error == ProductivityCalculatorError::none) {
        bool left = false;
        if (productivity_calculator_take_two(parser, '<', '<')) left = true;
        else if (!productivity_calculator_take_two(parser, '>', '>')) break;
        if (!parser.allow_bitwise) { parser.error = ProductivityCalculatorError::syntax; break; }
        const int32_t count = productivity_calculator_parse_add(parser);
        if (count < 0 || count > 31) { parser.error = ProductivityCalculatorError::shift_range; break; }
        value = left ? static_cast<int32_t>(static_cast<uint32_t>(value) << static_cast<uint32_t>(count))
                     : static_cast<int32_t>(value >> static_cast<uint32_t>(count));
    }
    return value;
}

int32_t productivity_calculator_parse_and(ProductivityCalculatorParser& parser) {
    int32_t value = productivity_calculator_parse_shift(parser);
    while (parser.error == ProductivityCalculatorError::none) {
        productivity_calculator_skip_spaces(parser);
        if (parser.position >= parser.size || parser.input[parser.position] != '&') break;
        ++parser.position;
        if (!parser.allow_bitwise) { parser.error = ProductivityCalculatorError::syntax; break; }
        value = static_cast<int32_t>(static_cast<uint32_t>(value) & static_cast<uint32_t>(productivity_calculator_parse_shift(parser)));
    }
    return value;
}

int32_t productivity_calculator_parse_xor(ProductivityCalculatorParser& parser) {
    int32_t value = productivity_calculator_parse_and(parser);
    while (parser.error == ProductivityCalculatorError::none) {
        if (!productivity_calculator_take(parser, '^')) break;
        if (!parser.allow_bitwise) { parser.error = ProductivityCalculatorError::syntax; break; }
        value = static_cast<int32_t>(static_cast<uint32_t>(value) ^ static_cast<uint32_t>(productivity_calculator_parse_and(parser)));
    }
    return value;
}

int32_t productivity_calculator_parse_or(ProductivityCalculatorParser& parser) {
    int32_t value = productivity_calculator_parse_xor(parser);
    while (parser.error == ProductivityCalculatorError::none) {
        productivity_calculator_skip_spaces(parser);
        if (parser.position >= parser.size || parser.input[parser.position] != '|') break;
        ++parser.position;
        if (!parser.allow_bitwise) { parser.error = ProductivityCalculatorError::syntax; break; }
        value = static_cast<int32_t>(static_cast<uint32_t>(value) | static_cast<uint32_t>(productivity_calculator_parse_xor(parser)));
    }
    return value;
}

bool productivity_calculator_evaluate_expression(const char* input, uint8_t size, bool allow_bitwise,
                                                  int32_t& output, ProductivityCalculatorError& error) {
    ProductivityCalculatorParser parser{input, size, 0U, allow_bitwise, ProductivityCalculatorError::none};
    output = productivity_calculator_parse_or(parser);
    productivity_calculator_skip_spaces(parser);
    if (parser.error == ProductivityCalculatorError::none && parser.position != parser.size)
        parser.error = ProductivityCalculatorError::syntax;
    error = parser.error;
    return error == ProductivityCalculatorError::none;
}

bool productivity_calculator_input_allowed(uint16_t key) {
    if (key >= '0' && key <= '9') return true;
    if (key >= 'a' && key <= 'f') return true;
    if (key >= 'A' && key <= 'F') return true;
    return key == 'x' || key == 'X' || key == 'b' || key == 'B' || key == ' ' || key == '+' || key == '-' ||
        key == '*' || key == '/' || key == '%' || key == '(' || key == ')' || key == '&' || key == '|' ||
        key == '^' || key == '~' || key == '<' || key == '>';
}
