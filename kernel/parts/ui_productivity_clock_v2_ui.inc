void productivity_clock_v2_render() {
    draw_window_chrome("ZEN CLOCK", "ALARMS TIMERS AND LAPS");
    uint8_t hour = 0U, minute = 0U, weekday = 0U, day = 0U, month = 0U, year = 0U;
    const bool time_ok = rtc_read_time(hour, minute);
    const bool date_ok = rtc_read_date(weekday, day, month, year);
    const int32_t x = window_x + 16, y = window_y + 64;

    fill_round_rect(x, y, 480, 70, 9, current_theme.surface_low);
    draw_text(x + 14, y + 14, "LOCAL", current_theme.text_muted, 1);
    if (time_ok) {
        draw_two_digits(x + 14, y + 38, hour, current_theme.text);
        draw_char(x + 28, y + 38, ':', current_theme.text_muted, 1);
        draw_two_digits(x + 36, y + 38, minute, current_theme.text);
    } else draw_text(x + 14, y + 38, "RTC UNAVAILABLE", current_theme.warning, 1);
    if (date_ok) {
        char date[11]{};
        productivity_date_text(date, static_cast<uint16_t>(2000U + year), month, day);
        draw_text_right(x + 462, y + 38, date, current_theme.accent, 1);
    }

    fill_round_rect(x, y + 80, 154, 214, 9, current_theme.surface_high);
    draw_text(x + 12, y + 94, "STOPWATCH", current_theme.text, 1);
    const uint32_t ticks = productivity_stopwatch_ticks();
    productivity_draw_duration(x + 12, y + 124, ticks / 100U, current_theme.accent, true, ticks);
    draw_text(x + 12, y + 150, productivity_stopwatch_running ? "RUNNING" : "PAUSED", productivity_stopwatch_running ? current_theme.success : current_theme.text_muted, 1);
    draw_text(x + 12, y + 172, "LAPS", current_theme.text_muted, 1);
    const uint8_t first_lap = productivity_clock_v2_lap_count > 4U ? static_cast<uint8_t>(productivity_clock_v2_lap_count - 4U) : 0U;
    int32_t lap_y = y + 190;
    for (uint8_t index = first_lap; index < productivity_clock_v2_lap_count; ++index) {
        draw_uint(x + 12, lap_y, static_cast<uint32_t>(index + 1U), current_theme.text_muted);
        productivity_draw_duration(x + 30, lap_y, productivity_clock_v2_laps[index] / 100U, current_theme.text, true, productivity_clock_v2_laps[index]);
        lap_y += 18;
    }
    if (!productivity_clock_v2_lap_count) draw_text(x + 12, y + 194, "NO LAPS", current_theme.text_muted, 1);
    draw_text(x + 12, y + 276, "F1 RUN  F2 RESET  L LAP", current_theme.text_muted, 1);

    fill_round_rect(x + 164, y + 80, 146, 214, 9, current_theme.surface_high);
    draw_text(x + 176, y + 94, "TIMERS", current_theme.text, 1);
    for (uint8_t index = 0U; index < productivity_clock_v2_timer_capacity; ++index) {
        const int32_t row_y = y + 118 + static_cast<int32_t>(index * 50U);
        const bool focused = index == productivity_clock_v2_timer_focus;
        if (focused) fill_round_rect(x + 172, row_y - 8, 130, 42, 6, current_theme.accent_soft);
        draw_uint(x + 180, row_y, static_cast<uint32_t>(index + 1U), focused ? current_theme.accent : current_theme.text_muted);
        const uint32_t remaining = productivity_clock_v2_timer_remaining(index);
        productivity_draw_duration(x + 200, row_y, remaining, productivity_clock_v2_timers[index].running ? current_theme.success : current_theme.text, false);
        draw_text(x + 180, row_y + 20, productivity_clock_v2_timers[index].running ? "RUNNING" : (remaining ? "READY" : "EMPTY"), productivity_clock_v2_timers[index].running ? current_theme.success : current_theme.text_muted, 1);
    }
    draw_text(x + 176, y + 276, "LEFT/RIGHT SLOT", current_theme.text_muted, 1);
    draw_text(x + 176, y + 292, "F3 +1M  F4 RUN", current_theme.text_muted, 1);

    fill_round_rect(x + 320, y + 80, 160, 214, 9, current_theme.surface_high);
    draw_text(x + 332, y + 94, "ALARMS", current_theme.text, 1);
    int32_t alarm_y = y + 118;
    const uint8_t start = productivity_clock_v2_alarm_focus >= 4U ? static_cast<uint8_t>(productivity_clock_v2_alarm_focus - 3U) : 0U;
    for (uint8_t row = 0U; row < 4U; ++row) {
        const uint8_t index = static_cast<uint8_t>(start + row);
        if (index >= productivity_clock_v2_alarm_count) break;
        const ProductivityClockAlarmV2& alarm = productivity_clock_v2_alarms[index];
        const bool focused = index == productivity_clock_v2_alarm_focus;
        if (focused) fill_round_rect(x + 328, alarm_y - 8, 144, 38, 6, current_theme.accent_soft);
        draw_two_digits(x + 336, alarm_y, alarm.hour, alarm.enabled ? current_theme.accent : current_theme.text_muted);
        draw_char(x + 350, alarm_y, ':', current_theme.text_muted, 1);
        draw_two_digits(x + 358, alarm_y, alarm.minute, alarm.enabled ? current_theme.accent : current_theme.text_muted);
        char title[13]{};
        string_copy(title, alarm.title, sizeof(title));
        draw_text(x + 382, alarm_y, title, alarm.enabled ? current_theme.text : current_theme.text_muted, 1);
        draw_text(x + 336, alarm_y + 18, productivity_clock_v2_mask_text(alarm.day_mask), current_theme.text_muted, 1);
        alarm_y += 44;
    }
    if (!productivity_clock_v2_alarm_count) draw_text(x + 332, y + 132, "NO ALARMS", current_theme.text_muted, 1);
    draw_text(x + 332, y + 276, "UP/DOWN SELECT", current_theme.text_muted, 1);
    draw_text(x + 332, y + 292, "A ADD E EDIT T TOGGLE", current_theme.text_muted, 1);

    if (productivity_clock_v2_alarm_editing) {
        fill_round_rect(x, y + 306, 480, 38, 8, current_theme.accent_soft);
        draw_text(x + 10, y + 320, productivity_clock_v2_alarm_edit_existing ? "EDIT" : "ADD", current_theme.accent, 1);
        char preview[59]{};
        string_copy(preview, productivity_clock_v2_input_size ? productivity_clock_v2_input : "07:30 WAKE UP !WEEKDAYS", sizeof(preview));
        draw_text(x + 50, y + 320, preview, productivity_clock_v2_input_size ? current_theme.text : current_theme.text_muted, 1);
    } else {
        draw_text(x, y + 316, "DELETE REMOVE ALARM   ESC CLOSE   ALARMS PERSIST IN ZENOVFS", current_theme.text_muted, 1);
    }
}

bool productivity_clock_v2_handle_alarm_input(uint16_t key) {
    if (key == key_escape) {
        productivity_clock_v2_alarm_editing = false;
        productivity_clock_v2_alarm_edit_existing = false;
        productivity_clock_v2_input_size = 0U;
        productivity_clock_v2_input[0] = 0;
    } else if (key == '\b' && productivity_clock_v2_input_size) {
        productivity_clock_v2_input[--productivity_clock_v2_input_size] = 0;
    } else if (key == '\n') {
        (void)productivity_clock_v2_commit_alarm();
    } else if (key >= 32U && key <= 126U && key != '|' && productivity_clock_v2_input_size + 1U < sizeof(productivity_clock_v2_input)) {
        productivity_clock_v2_input[productivity_clock_v2_input_size++] = static_cast<char>(key);
        productivity_clock_v2_input[productivity_clock_v2_input_size] = 0;
    }
    return true;
}

bool productivity_clock_v2_handle_key(uint16_t key) {
    if (productivity_clock_v2_alarm_editing) return productivity_clock_v2_handle_alarm_input(key);
    if (key == key_f1 || key == ' ') productivity_clock_toggle_stopwatch();
    else if (key == key_f2) productivity_clock_v2_reset_stopwatch();
    else if (key == 'l' || key == 'L') productivity_clock_v2_add_lap();
    else if (key == key_f3) productivity_clock_v2_add_minute();
    else if (key == key_f4 || key == '\n') productivity_clock_v2_toggle_timer();
    else if (key == key_left) productivity_clock_v2_timer_focus = productivity_clock_v2_timer_focus == 0U ? static_cast<uint8_t>(productivity_clock_v2_timer_capacity - 1U) : static_cast<uint8_t>(productivity_clock_v2_timer_focus - 1U);
    else if (key == key_right || key == '\t') productivity_clock_v2_timer_focus = static_cast<uint8_t>((productivity_clock_v2_timer_focus + 1U) % productivity_clock_v2_timer_capacity);
    else if (key == key_up && productivity_clock_v2_alarm_count) productivity_clock_v2_alarm_focus = productivity_clock_v2_alarm_focus == 0U ? static_cast<uint8_t>(productivity_clock_v2_alarm_count - 1U) : static_cast<uint8_t>(productivity_clock_v2_alarm_focus - 1U);
    else if (key == key_down && productivity_clock_v2_alarm_count) productivity_clock_v2_alarm_focus = static_cast<uint8_t>((productivity_clock_v2_alarm_focus + 1U) % productivity_clock_v2_alarm_count);
    else if (key == 'a' || key == 'A') productivity_clock_v2_begin_alarm_add();
    else if (key == 'e' || key == 'E') productivity_clock_v2_begin_alarm_edit();
    else if ((key == 't' || key == 'T') && productivity_clock_v2_alarm_count) {
        ProductivityClockAlarmV2& alarm = productivity_clock_v2_alarms[productivity_clock_v2_alarm_focus];
        alarm.enabled = !alarm.enabled;
        alarm.last_fired_stamp = 0xFFFFFFFFU;
        (void)productivity_clock_v2_save_alarms();
        serial::line("UI_CLOCK_V2_ALARM_TOGGLE_OK");
    } else if (key == key_delete) (void)productivity_clock_v2_delete_alarm();
    else return key != key_escape;
    return true;
}
