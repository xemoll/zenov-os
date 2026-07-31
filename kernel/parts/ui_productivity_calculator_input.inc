bool productivity_calculator_handle_key(uint16_t key) {
    if (key == key_escape) { productivity_utility_close(); return true; }
    if (key == key_f1) { productivity_calculator_select_mode(ProductivityCalculatorMode::standard); return true; }
    if (key == key_f2) { productivity_calculator_select_mode(ProductivityCalculatorMode::programmer); return true; }
    if (key == key_f3) { productivity_calculator_select_mode(ProductivityCalculatorMode::date); return true; }
    if (key == key_f4) { productivity_calculator_select_mode(ProductivityCalculatorMode::units); return true; }
    if (productivity_calculator_mode == ProductivityCalculatorMode::date) {
        if (key == '\t' || key == key_left || key == key_right) productivity_calculator_date_select_b = !productivity_calculator_date_select_b;
        else if (key == key_up) productivity_calculator_adjust_selected_date(1);
        else if (key == key_down) productivity_calculator_adjust_selected_date(-1);
        else if (key == key_f5) productivity_calculator_adjust_selected_date(-30);
        else if (key == key_f6) productivity_calculator_adjust_selected_date(30);
        else if (key == key_f7) productivity_calculator_adjust_selected_date(-365);
        else if (key == key_f8) productivity_calculator_adjust_selected_date(365);
        return true;
    }
    if (productivity_calculator_mode == ProductivityCalculatorMode::units) {
        if (key == key_f5) productivity_unit_group = static_cast<ProductivityUnitGroup>((static_cast<uint8_t>(productivity_unit_group) + 1U) % 3U);
        else if (key == key_f6) ++productivity_unit_from;
        else if (key == key_f7) ++productivity_unit_to;
        else if (key == key_delete) { productivity_calculator_input_size = 0U; productivity_calculator_input[0] = 0; }
        else if (key == '\b' && productivity_calculator_input_size) productivity_calculator_input[--productivity_calculator_input_size] = 0;
        else if ((key == '-' && !productivity_calculator_input_size) || (key >= '0' && key <= '9')) {
            if (productivity_calculator_input_size + 1U < sizeof(productivity_calculator_input)) {
                productivity_calculator_input[productivity_calculator_input_size++] = static_cast<char>(key);
                productivity_calculator_input[productivity_calculator_input_size] = 0;
            }
        }
        return true;
    }
    if (key == key_f5) { productivity_calculator_memory = 0; (void)productivity_calculator_save_state(); return true; }
    if (key == key_f6) { productivity_calculator_set_input_from_int(productivity_calculator_memory); return true; }
    if (key == key_f7) {
        if (productivity_calculator_has_result && productivity_calculator_checked_add(productivity_calculator_memory, productivity_calculator_result, productivity_calculator_memory))
            (void)productivity_calculator_save_state();
        return true;
    }
    if (key == key_f8) {
        if (productivity_calculator_has_result && productivity_calculator_checked_sub(productivity_calculator_memory, productivity_calculator_result, productivity_calculator_memory))
            (void)productivity_calculator_save_state();
        return true;
    }
    if (key == '\n' || key == '=') { productivity_calculator_evaluate(); return true; }
    if (key == key_delete) {
        productivity_calculator_input_size = 0U; productivity_calculator_input[0] = 0;
        productivity_calculator_has_result = false; productivity_calculator_error = ProductivityCalculatorError::none;
        return true;
    }
    if (key == '\b' && productivity_calculator_input_size) {
        productivity_calculator_input[--productivity_calculator_input_size] = 0;
        productivity_calculator_error = ProductivityCalculatorError::none;
        return true;
    }
    if (key >= 32U && key <= 126U && productivity_calculator_input_size + 1U < sizeof(productivity_calculator_input) &&
        productivity_calculator_input_allowed(key)) {
        productivity_calculator_input[productivity_calculator_input_size++] = static_cast<char>(key);
        productivity_calculator_input[productivity_calculator_input_size] = 0;
        productivity_calculator_has_result = false;
        productivity_calculator_error = ProductivityCalculatorError::none;
    }
    return true;
}
