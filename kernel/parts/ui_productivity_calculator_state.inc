bool productivity_calculator_save_state() {
    if (!productivity_utilities_ensure_storage()) return false;
    char data[1024]{};
    uint32_t length = 0U;
    if (!productivity_append_text(data, sizeof(data), length, "C1\t")) return false;
    productivity_utility_append_int32(data, sizeof(data), length, productivity_calculator_memory);
    if (!productivity_append_text(data, sizeof(data), length, "\n")) return false;
    for (uint8_t index = 0U; index < productivity_calculator_history_count; ++index) {
        if (!productivity_append_text(data, sizeof(data), length, "H\t") ||
            !productivity_append_text(data, sizeof(data), length, productivity_calculator_history_expression[index]) ||
            !productivity_append_text(data, sizeof(data), length, "\t")) return false;
        productivity_utility_append_int32(data, sizeof(data), length, productivity_calculator_history_result[index]);
        if (!productivity_append_text(data, sizeof(data), length, "\n")) return false;
    }
    const bool saved = storage::guarded_write_file("/utilities/calculator.state", reinterpret_cast<const uint8_t*>(data), length, false);
    serial::line(saved ? "UI_CALCULATOR_STATE_SAVE_OK" : "UI_CALCULATOR_STATE_SAVE_FAILED");
    return saved;
}

void productivity_calculator_load_state() {
    productivity_calculator_memory = 0;
    productivity_calculator_history_count = 0U;
    memset(productivity_calculator_history_expression, 0, sizeof(productivity_calculator_history_expression));
    if (!productivity_utilities_ensure_storage()) return;
    char data[1024]{};
    uint32_t size = 0U;
    if (!storage::security_read_file("/utilities/calculator.state", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) return;
    data[size] = 0;
    uint32_t position = 0U;
    while (position < size) {
        const uint32_t start = position;
        while (position < size && data[position] != '\n') ++position;
        const uint32_t end = position;
        if (position < size) ++position;
        if (end > start + 3U && data[start] == 'C' && data[start + 1U] == '1' && data[start + 2U] == '\t') {
            int32_t value = 0;
            if (productivity_utility_parse_signed(data + start + 3U, end - start - 3U, value)) productivity_calculator_memory = value;
            continue;
        }
        if (productivity_calculator_history_count >= productivity_calculator_history_capacity || end <= start + 4U ||
            data[start] != 'H' || data[start + 1U] != '\t') continue;
        uint32_t separator = start + 2U;
        while (separator < end && data[separator] != '\t') ++separator;
        if (separator >= end || separator == start + 2U || separator - (start + 2U) >= productivity_calculator_input_capacity) continue;
        int32_t result = 0;
        if (!productivity_utility_parse_signed(data + separator + 1U, end - separator - 1U, result)) continue;
        const uint8_t destination = productivity_calculator_history_count++;
        uint32_t copied = 0U;
        for (uint32_t source = start + 2U; source < separator && copied + 1U < productivity_calculator_input_capacity; ++source) {
            const char value = data[source];
            if (value < 32 || value > 126) continue;
            productivity_calculator_history_expression[destination][copied++] = value;
        }
        productivity_calculator_history_expression[destination][copied] = 0;
        productivity_calculator_history_result[destination] = result;
    }
}

void productivity_calculator_push_history(const char* expression, int32_t result) {
    if (productivity_calculator_history_count < productivity_calculator_history_capacity) ++productivity_calculator_history_count;
    for (uint8_t index = static_cast<uint8_t>(productivity_calculator_history_count - 1U); index > 0U; --index) {
        string_copy(productivity_calculator_history_expression[index], productivity_calculator_history_expression[index - 1U], productivity_calculator_input_capacity);
        productivity_calculator_history_result[index] = productivity_calculator_history_result[index - 1U];
    }
    string_copy(productivity_calculator_history_expression[0], expression, productivity_calculator_input_capacity);
    productivity_calculator_history_result[0] = result;
}

void productivity_calculator_evaluate() {
    if (!productivity_calculator_input_size) return;
    int32_t result = 0;
    ProductivityCalculatorError error = ProductivityCalculatorError::none;
    const bool programmer = productivity_calculator_mode == ProductivityCalculatorMode::programmer;
    if (!productivity_calculator_evaluate_expression(productivity_calculator_input, productivity_calculator_input_size, programmer, result, error)) {
        productivity_calculator_error = error;
        productivity_calculator_has_result = false;
        serial::line("UI_CALCULATOR_EVAL_FAILED");
        return;
    }
    productivity_calculator_result = result;
    productivity_calculator_has_result = true;
    productivity_calculator_error = ProductivityCalculatorError::none;
    productivity_calculator_push_history(productivity_calculator_input, result);
    (void)productivity_calculator_save_state();
    serial::line("UI_CALCULATOR_EVAL_OK");
}

void productivity_calculator_set_input_from_int(int32_t value) {
    char text[16]{};
    productivity_utility_format_int32(text, sizeof(text), value);
    string_copy(productivity_calculator_input, text, sizeof(productivity_calculator_input));
    productivity_calculator_input_size = static_cast<uint8_t>(string_length(productivity_calculator_input));
    productivity_calculator_error = ProductivityCalculatorError::none;
    productivity_calculator_has_result = false;
}
