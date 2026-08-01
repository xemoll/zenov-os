enum class CalcError : std::uint8_t { none, syntax, overflow, divide_by_zero, shift_range };

struct Parser {
    std::string_view input;
    std::size_t pos{};
    bool allow_bitwise{};
    CalcError error{CalcError::none};

    void spaces() {
        while (pos < input.size() && input[pos] == ' ') ++pos;
    }

    bool take(char c) {
        spaces();
        if (pos < input.size() && input[pos] == c) { ++pos; return true; }
        return false;
    }

    bool take2(char a, char b) {
        spaces();
        if (pos + 1 < input.size() && input[pos] == a && input[pos + 1] == b) {
            pos += 2;
            return true;
        }
        return false;
    }
};

bool checked_add(std::int32_t a, std::int32_t b, std::int32_t& out) {
    if ((b > 0 && a > std::numeric_limits<std::int32_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int32_t>::min() - b)) return false;
    out = static_cast<std::int32_t>(a + b);
    return true;
}

bool checked_sub(std::int32_t a, std::int32_t b, std::int32_t& out) {
    if ((b < 0 && a > std::numeric_limits<std::int32_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<std::int32_t>::min() + b)) return false;
    out = static_cast<std::int32_t>(a - b);
    return true;
}

bool checked_mul(std::int32_t a, std::int32_t b, std::int32_t& out) {
    if (a == 0 || b == 0) { out = 0; return true; }
    if ((a == -1 && b == std::numeric_limits<std::int32_t>::min()) ||
        (b == -1 && a == std::numeric_limits<std::int32_t>::min())) return false;
    if (a > 0) {
        if (b > 0) { if (a > std::numeric_limits<std::int32_t>::max() / b) return false; }
        else { if (b < std::numeric_limits<std::int32_t>::min() / a) return false; }
    } else {
        if (b > 0) { if (a < std::numeric_limits<std::int32_t>::min() / b) return false; }
        else { if (a < std::numeric_limits<std::int32_t>::max() / b) return false; }
    }
    out = static_cast<std::int32_t>(a * b);
    return true;
}

std::int32_t parse_or(Parser&);

std::int32_t parse_primary(Parser& p) {
    p.spaces();
    if (p.take('(')) {
        const auto value = parse_or(p);
        if (!p.take(')') && p.error == CalcError::none) p.error = CalcError::syntax;
        return value;
    }
    unsigned base = 10;
    if (p.pos + 2 <= p.input.size() && p.input[p.pos] == '0' &&
        (p.input[p.pos + 1] == 'x' || p.input[p.pos + 1] == 'X')) {
        base = 16; p.pos += 2;
    } else if (p.pos + 2 <= p.input.size() && p.input[p.pos] == '0' &&
               (p.input[p.pos + 1] == 'b' || p.input[p.pos + 1] == 'B')) {
        base = 2; p.pos += 2;
    }
    std::uint32_t value = 0;
    std::size_t digits = 0;
    while (p.pos < p.input.size()) {
        const char c = p.input[p.pos];
        unsigned digit = 99;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = 10U + static_cast<unsigned>(c - 'a');
        else if (c >= 'A' && c <= 'F') digit = 10U + static_cast<unsigned>(c - 'A');
        if (digit >= base) break;
        if (value > (static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) - digit) / base) {
            p.error = CalcError::overflow;
            return 0;
        }
        value = value * base + digit;
        ++p.pos;
        ++digits;
    }
    if (!digits) p.error = CalcError::syntax;
    return static_cast<std::int32_t>(value);
}

std::int32_t parse_unary(Parser& p) {
    if (p.take('+')) return parse_unary(p);
    if (p.take('-')) {
        const auto value = parse_unary(p);
        if (value == std::numeric_limits<std::int32_t>::min()) { p.error = CalcError::overflow; return 0; }
        return static_cast<std::int32_t>(-value);
    }
    if (p.take('~')) {
        if (!p.allow_bitwise) { p.error = CalcError::syntax; return 0; }
        return static_cast<std::int32_t>(~static_cast<std::uint32_t>(parse_unary(p)));
    }
    return parse_primary(p);
}

std::int32_t parse_mul(Parser& p) {
    auto value = parse_unary(p);
    while (p.error == CalcError::none) {
        if (p.take('*')) {
            std::int32_t result{};
            if (!checked_mul(value, parse_unary(p), result)) p.error = CalcError::overflow;
            value = result;
        } else if (p.take('/')) {
            const auto right = parse_unary(p);
            if (right == 0) p.error = CalcError::divide_by_zero;
            else if (value == std::numeric_limits<std::int32_t>::min() && right == -1) p.error = CalcError::overflow;
            else value = static_cast<std::int32_t>(value / right);
        } else if (p.take('%')) {
            const auto right = parse_unary(p);
            if (right == 0) p.error = CalcError::divide_by_zero;
            else if (value == std::numeric_limits<std::int32_t>::min() && right == -1) value = 0;
            else value = static_cast<std::int32_t>(value % right);
        } else break;
    }
    return value;
}

std::int32_t parse_add(Parser& p) {
    auto value = parse_mul(p);
    while (p.error == CalcError::none) {
        if (p.take('+')) {
            std::int32_t result{};
            if (!checked_add(value, parse_mul(p), result)) p.error = CalcError::overflow;
            value = result;
        } else if (p.take('-')) {
            std::int32_t result{};
            if (!checked_sub(value, parse_mul(p), result)) p.error = CalcError::overflow;
            value = result;
        } else break;
    }
    return value;
}

std::int32_t parse_shift(Parser& p) {
    auto value = parse_add(p);
    while (p.error == CalcError::none) {
        bool left = false;
        if (p.take2('<', '<')) left = true;
        else if (!p.take2('>', '>')) break;
        if (!p.allow_bitwise) { p.error = CalcError::syntax; break; }
        const auto count = parse_add(p);
        if (count < 0 || count > 31) { p.error = CalcError::shift_range; break; }
        if (left) value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << static_cast<unsigned>(count));
        else value = static_cast<std::int32_t>(value >> static_cast<unsigned>(count));
    }
    return value;
}

std::int32_t parse_and(Parser& p) {
    auto value = parse_shift(p);
    while (p.error == CalcError::none) {
        p.spaces();
        if (p.pos >= p.input.size() || p.input[p.pos] != '&') break;
        ++p.pos;
        if (!p.allow_bitwise) { p.error = CalcError::syntax; break; }
        value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(parse_shift(p)));
    }
    return value;
}

std::int32_t parse_xor(Parser& p) {
    auto value = parse_and(p);
    while (p.error == CalcError::none) {
        if (!p.take('^')) break;
        if (!p.allow_bitwise) { p.error = CalcError::syntax; break; }
        value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) ^ static_cast<std::uint32_t>(parse_and(p)));
    }
    return value;
}

std::int32_t parse_or(Parser& p) {
    auto value = parse_xor(p);
    while (p.error == CalcError::none) {
        p.spaces();
        if (p.pos >= p.input.size() || p.input[p.pos] != '|') break;
        ++p.pos;
        if (!p.allow_bitwise) { p.error = CalcError::syntax; break; }
        value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) | static_cast<std::uint32_t>(parse_xor(p)));
    }
    return value;
}

std::pair<std::int32_t, CalcError> eval(std::string_view expression, bool bitwise) {
    Parser parser{expression, 0, bitwise, CalcError::none};
    const auto value = parse_or(parser);
    parser.spaces();
    if (parser.error == CalcError::none && parser.pos != expression.size()) parser.error = CalcError::syntax;
    return {value, parser.error};
}
