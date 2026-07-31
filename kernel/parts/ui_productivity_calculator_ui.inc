const char* productivity_calculator_mode_text() {
    if (productivity_calculator_mode == ProductivityCalculatorMode::programmer) return "PROGRAMMER";
    if (productivity_calculator_mode == ProductivityCalculatorMode::date) return "DATE";
    if (productivity_calculator_mode == ProductivityCalculatorMode::units) return "UNITS";
    return "STANDARD";
}

void productivity_calculator_format_hex(char output[12], int32_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    const uint32_t bits = static_cast<uint32_t>(value);
    output[0] = '0'; output[1] = 'x';
    for (uint8_t index = 0U; index < 8U; ++index) {
        const uint8_t shift = static_cast<uint8_t>((7U - index) * 4U);
        output[2U + index] = digits[(bits >> shift) & 0xFU];
    }
    output[10] = 0;
}

const char* productivity_unit_group_text() {
    if (productivity_unit_group == ProductivityUnitGroup::temperature) return "TEMPERATURE";
    if (productivity_unit_group == ProductivityUnitGroup::storage) return "STORAGE";
    return "LENGTH";
}

const char* productivity_unit_name(uint8_t index) {
    static constexpr const char* length_names[6] = {"MM", "CM", "M", "KM", "IN", "FT"};
    static constexpr const char* temperature_names[3] = {"C", "F", "K"};
    static constexpr const char* storage_names[4] = {"B", "KIB", "MIB", "GIB"};
    if (productivity_unit_group == ProductivityUnitGroup::temperature) return temperature_names[index % 3U];
    if (productivity_unit_group == ProductivityUnitGroup::storage) return storage_names[index % 4U];
    return length_names[index % 6U];
}

void productivity_calculator_render() {
    draw_window_chrome("ZEN CALCULATOR", "BOUNDED LOCAL COMPUTE");
    const int32_t x = window_x + 16, y = window_y + 62;
    fill_round_rect(x, y, 480, 38, 8, current_theme.surface_low);
    draw_text(x + 14, y + 15, productivity_calculator_mode_text(), current_theme.accent, 1);
    draw_text_right(x + 462, y + 15, "F1 STD  F2 PROG  F3 DATE  F4 UNITS", current_theme.text_muted, 1);

    fill_round_rect(x, y + 48, 480, 104, 8, current_theme.surface_high);
    if (productivity_calculator_mode == ProductivityCalculatorMode::date) {
        char left[11]{}, right[11]{};
        productivity_date_text(left, productivity_calculator_date_a.year, productivity_calculator_date_a.month, productivity_calculator_date_a.day);
        productivity_date_text(right, productivity_calculator_date_b.year, productivity_calculator_date_b.month, productivity_calculator_date_b.day);
        draw_text(x + 18, y + 70, productivity_calculator_date_select_b ? "FROM" : "FROM *", current_theme.text_muted, 1);
        draw_text(x + 18, y + 94, left, productivity_calculator_date_select_b ? current_theme.text : current_theme.accent, 2);
        draw_text(x + 258, y + 70, productivity_calculator_date_select_b ? "TO *" : "TO", current_theme.text_muted, 1);
        draw_text(x + 258, y + 94, right, productivity_calculator_date_select_b ? current_theme.accent : current_theme.text, 2);
        draw_text(x + 18, y + 132, "DIFFERENCE", current_theme.text_muted, 1);
        draw_uint(x + 102, y + 132, productivity_calculator_date_difference(), current_theme.success);
        draw_text(x + 170, y + 132, "DAYS", current_theme.success, 1);
    } else if (productivity_calculator_mode == ProductivityCalculatorMode::units) {
        draw_text(x + 18, y + 70, productivity_unit_group_text(), current_theme.text_muted, 1);
        draw_text(x + 18, y + 98, productivity_calculator_input_size ? productivity_calculator_input : "0", current_theme.text, 2);
        draw_text(x + 190, y + 102, productivity_unit_name(productivity_unit_from), current_theme.accent, 1);
        draw_text(x + 246, y + 102, "TO", current_theme.text_muted, 1);
        int32_t value = 0; bool negative = false; uint64_t magnitude = 0U; char converted[32]{};
        if (productivity_utility_parse_signed(productivity_calculator_input, productivity_calculator_input_size, value) &&
            productivity_calculator_unit_scaled(value, negative, magnitude)) productivity_calculator_format_scaled(converted, negative, magnitude);
        else string_copy(converted, "INVALID/RANGE", sizeof(converted));
        draw_text(x + 284, y + 98, converted, current_theme.success, 2);
        draw_text_right(x + 462, y + 128, productivity_unit_name(productivity_unit_to), current_theme.accent, 1);
    } else {
        draw_text(x + 18, y + 70, "EXPRESSION", current_theme.text_muted, 1);
        draw_text(x + 18, y + 94, productivity_calculator_input_size ? productivity_calculator_input : "TYPE AN EXPRESSION", productivity_calculator_input_size ? current_theme.text : current_theme.text_muted, 2);
        if (productivity_calculator_error != ProductivityCalculatorError::none)
            draw_text(x + 18, y + 132, productivity_calculator_error_text(), current_theme.warning, 1);
        else if (productivity_calculator_has_result) {
            draw_text(x + 18, y + 132, "RESULT", current_theme.text_muted, 1);
            productivity_utility_draw_int32(x + 82, y + 132, productivity_calculator_result, current_theme.success, 1);
            if (productivity_calculator_mode == ProductivityCalculatorMode::programmer) {
                char hex[12]{}; productivity_calculator_format_hex(hex, productivity_calculator_result);
                draw_text_right(x + 462, y + 132, hex, current_theme.accent, 1);
            }
        }
    }

    fill_round_rect(x, y + 164, 480, 130, 8, current_theme.surface_low);
    if (productivity_calculator_mode == ProductivityCalculatorMode::standard || productivity_calculator_mode == ProductivityCalculatorMode::programmer) {
        draw_text(x + 14, y + 184, "HISTORY", current_theme.text_muted, 1);
        char memory[24]{}; uint32_t memory_length = 0U;
        productivity_append_text(memory, sizeof(memory), memory_length, "MEM ");
        productivity_utility_append_int32(memory, sizeof(memory), memory_length, productivity_calculator_memory);
        draw_text_right(x + 462, y + 184, memory, current_theme.accent, 1);
        const uint8_t rows = productivity_calculator_history_count < 4U ? productivity_calculator_history_count : 4U;
        for (uint8_t row = 0U; row < rows; ++row) {
            const int32_t row_y = y + 210 + static_cast<int32_t>(row * 22U);
            char expression[33]{}; string_copy(expression, productivity_calculator_history_expression[row], sizeof(expression));
            draw_text(x + 16, row_y, expression, current_theme.text, 1);
            productivity_utility_draw_int32(x + 372, row_y, productivity_calculator_history_result[row], current_theme.text_muted, 1);
        }
        if (!rows) draw_text(x + 16, y + 226, "NO CALCULATIONS YET", current_theme.text_muted, 1);
    } else if (productivity_calculator_mode == ProductivityCalculatorMode::date) {
        draw_text(x + 14, y + 184, "TAB SELECTS DATE", current_theme.text_muted, 1);
        draw_text(x + 14, y + 212, "UP/DOWN: +/- 1 DAY", current_theme.text, 1);
        draw_text(x + 14, y + 238, "F5/F6: +/- 30 DAYS", current_theme.text, 1);
        draw_text(x + 14, y + 264, "F7/F8: +/- 365 DAYS", current_theme.text, 1);
    } else {
        draw_text(x + 14, y + 184, "F5 GROUP  F6 FROM  F7 TO", current_theme.text_muted, 1);
        draw_text(x + 14, y + 214, "LENGTH: MM CM M KM IN FT", current_theme.text, 1);
        draw_text(x + 14, y + 240, "TEMP: C F K", current_theme.text, 1);
        draw_text(x + 14, y + 266, "STORAGE: B KIB MIB GIB", current_theme.text, 1);
    }
    draw_text(x, y + 316, "ENTER EVALUATE  DEL CLEAR  ESC CLOSE", current_theme.text_muted, 1);
}

