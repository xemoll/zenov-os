void productivity_calculator_select_mode(ProductivityCalculatorMode mode) {
    productivity_calculator_mode = mode;
    productivity_calculator_error = ProductivityCalculatorError::none;
    productivity_calculator_has_result = false;
    if (mode == ProductivityCalculatorMode::date) {
        uint16_t year = 2026U; uint8_t month = 1U, day = 1U, hour = 0U, minute = 0U;
        (void)productivity_utility_now(year, month, day, hour, minute);
        productivity_calculator_date_a = ProductivityCalculatorDate{year, month, day};
        productivity_utility_add_minutes(year, month, day, hour, minute, 1440);
        productivity_calculator_date_b = ProductivityCalculatorDate{year, month, day};
        productivity_calculator_date_select_b = false;
    } else if (mode == ProductivityCalculatorMode::units && !productivity_calculator_input_size) {
        productivity_calculator_set_input_from_int(1);
    }
}

void productivity_calculator_adjust_selected_date(int32_t days) {
    ProductivityCalculatorDate& date = productivity_calculator_date_select_b ? productivity_calculator_date_b : productivity_calculator_date_a;
    uint8_t hour = 0U, minute = 0U;
    productivity_utility_add_minutes(date.year, date.month, date.day, hour, minute, days * 1440);
}

uint32_t productivity_calculator_date_difference() {
    const uint32_t left = productivity_utility_date_ordinal(productivity_calculator_date_a.year, productivity_calculator_date_a.month, productivity_calculator_date_a.day);
    const uint32_t right = productivity_utility_date_ordinal(productivity_calculator_date_b.year, productivity_calculator_date_b.month, productivity_calculator_date_b.day);
    return left > right ? left - right : right - left;
}

bool productivity_calculator_u64_mul_u32(uint64_t value, uint32_t factor, uint64_t& output) {
    output = 0U;
    while (factor) {
        if (factor & 1U) {
            if (output > 0xFFFFFFFFFFFFFFFFULL - value) return false;
            output += value;
        }
        factor >>= 1U;
        if (!factor) break;
        if (value > 0x7FFFFFFFFFFFFFFFULL) return false;
        value <<= 1U;
    }
    return true;
}

bool productivity_calculator_unit_scaled(int32_t input, bool& negative, uint64_t& magnitude) {
    negative = input < 0;
    const uint32_t absolute = negative ? 0U - static_cast<uint32_t>(input) : static_cast<uint32_t>(input);
    if (absolute > 1000000U) return false;
    if (productivity_unit_group == ProductivityUnitGroup::temperature) {
        if (absolute > 100000U) return false;
        int32_t scaled = 0;
        const uint8_t from = static_cast<uint8_t>(productivity_unit_from % 3U);
        const uint8_t to = static_cast<uint8_t>(productivity_unit_to % 3U);
        int32_t celsius_milli = 0;
        if (from == 0U) celsius_milli = input * 1000;
        else if (from == 1U) celsius_milli = static_cast<int32_t>((input - 32) * 5000 / 9);
        else celsius_milli = static_cast<int32_t>(input * 1000 - 273150);
        if (to == 0U) scaled = celsius_milli;
        else if (to == 1U) scaled = static_cast<int32_t>(celsius_milli * 9 / 5 + 32000);
        else scaled = static_cast<int32_t>(celsius_milli + 273150);
        negative = scaled < 0;
        magnitude = negative ? static_cast<uint64_t>(0U - static_cast<uint32_t>(scaled)) : static_cast<uint64_t>(static_cast<uint32_t>(scaled));
        return true;
    }
    static constexpr uint32_t length_factors[6] = {1000U, 10000U, 1000000U, 1000000000U, 25400U, 304800U};
    static constexpr uint32_t storage_factors[4] = {1U, 1024U, 1048576U, 1073741824U};
    const uint8_t count = productivity_unit_group == ProductivityUnitGroup::length ? 6U : 4U;
    const uint8_t from = static_cast<uint8_t>(productivity_unit_from % count);
    const uint8_t to = static_cast<uint8_t>(productivity_unit_to % count);
    const uint32_t from_factor = productivity_unit_group == ProductivityUnitGroup::length ? length_factors[from] : storage_factors[from];
    const uint32_t to_factor = productivity_unit_group == ProductivityUnitGroup::length ? length_factors[to] : storage_factors[to];
    uint64_t product = 0U;
    if (!productivity_calculator_u64_mul_u32(static_cast<uint64_t>(absolute), from_factor, product)) return false;
    if (!productivity_calculator_u64_mul_u32(product, 1000U, magnitude)) return false;
    magnitude /= to_factor;
    return true;
}

void productivity_calculator_format_scaled(char output[32], bool negative, uint64_t magnitude) {
    char reverse[24]{};
    uint32_t count = 0U;
    const uint32_t fraction = static_cast<uint32_t>(magnitude % 1000U);
    uint64_t whole = magnitude / 1000U;
    do {
        reverse[count++] = static_cast<char>('0' + whole % 10U);
        whole /= 10U;
    } while (whole && count < sizeof(reverse));
    uint32_t length = 0U;
    if (negative && magnitude && length + 1U < 32U) output[length++] = '-';
    while (count && length + 1U < 32U) output[length++] = reverse[--count];
    if (length + 4U < 32U) {
        output[length++] = '.';
        output[length++] = static_cast<char>('0' + fraction / 100U);
        output[length++] = static_cast<char>('0' + (fraction / 10U) % 10U);
        output[length++] = static_cast<char>('0' + fraction % 10U);
    }
    output[length] = 0;
}

const char* productivity_calculator_error_text() {
    if (productivity_calculator_error == ProductivityCalculatorError::overflow) return "OVERFLOW";
    if (productivity_calculator_error == ProductivityCalculatorError::divide_by_zero) return "DIVIDE BY ZERO";
    if (productivity_calculator_error == ProductivityCalculatorError::shift_range) return "SHIFT 0..31";
    if (productivity_calculator_error == ProductivityCalculatorError::syntax) return "SYNTAX ERROR";
    return "";
}
