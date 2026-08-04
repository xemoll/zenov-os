bool productivity_clock_v2_parse_alarm_line(const char* line, uint32_t size, ProductivityClockAlarmV2& alarm) {
    memset(&alarm, 0, sizeof(alarm));
    if (!line || size < 16U || size > 39U || line[0] != 'A' || line[1] != '1' || line[2] != '|' ||
        line[5] != ':' || line[8] != '|' || line[10] != '|' || line[14] != '|' ||
        (line[9] != '0' && line[9] != '1')) return false;
    uint32_t hour = 0U, minute = 0U, mask = 0U;
    if (!productivity_calendar_clock_v2_parse_digits(line, 3U, 2U, hour) ||
        !productivity_calendar_clock_v2_parse_digits(line, 6U, 2U, minute) ||
        !productivity_calendar_clock_v2_parse_digits(line, 11U, 3U, mask) ||
        hour > 23U || minute > 59U || mask > 127U) return false;
    const uint32_t title_size = size - 15U;
    if (!title_size || title_size >= sizeof(alarm.title)) return false;
    for (uint32_t index = 0U; index < title_size; ++index) {
        const char value = line[15U + index];
        if (value < 32 || value > 126 || value == '|') return false;
        alarm.title[index] = value;
    }
    alarm.title[title_size] = 0;
    alarm.hour = static_cast<uint8_t>(hour);
    alarm.minute = static_cast<uint8_t>(minute);
    alarm.day_mask = static_cast<uint8_t>(mask);
    alarm.enabled = line[9] == '1';
    alarm.last_fired_stamp = 0xFFFFFFFFU;
    return true;
}

bool productivity_clock_v2_save_alarms() {
    char data[768]{};
    uint32_t length = 0U;
    for (uint8_t index = 0U; index < productivity_clock_v2_alarm_count; ++index) {
        const ProductivityClockAlarmV2& alarm = productivity_clock_v2_alarms[index];
        const uint8_t title_size = static_cast<uint8_t>(string_length(alarm.title));
        if (!productivity_calendar_clock_v2_title_valid(alarm.title, title_size, sizeof(alarm.title))) return false;
        if (!productivity_append_text(data, sizeof(data), length, "A1|")) return false;
        productivity_append_uint(data, sizeof(data), length, alarm.hour, 2U);
        if (!productivity_append_text(data, sizeof(data), length, ":")) return false;
        productivity_append_uint(data, sizeof(data), length, alarm.minute, 2U);
        if (length + 3U >= sizeof(data)) return false;
        data[length++] = '|';
        data[length++] = alarm.enabled ? '1' : '0';
        data[length++] = '|';
        data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, alarm.day_mask, 3U);
        if (!productivity_append_text(data, sizeof(data), length, "|") ||
            !productivity_append_text(data, sizeof(data), length, alarm.title) ||
            !productivity_append_text(data, sizeof(data), length, "\n")) return false;
    }
    const bool saved = storage::guarded_write_file("/clock/alarms.db", reinterpret_cast<const uint8_t*>(data), length, false);
    serial::line(saved ? "UI_CLOCK_V2_ALARMS_SAVE_OK" : "UI_CLOCK_V2_ALARMS_SAVE_FAILED");
    return saved;
}

void productivity_clock_v2_load_alarms() {
    productivity_clock_v2_alarm_count = 0U;
    memset(productivity_clock_v2_alarms, 0, sizeof(productivity_clock_v2_alarms));
    char data[768]{};
    uint32_t size = 0U;
    if (!storage::security_read_file("/clock/alarms.db", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
        serial::line("UI_CLOCK_V2_ALARMS_EMPTY");
        return;
    }
    data[size] = 0;
    uint32_t position = 0U;
    while (position < size && productivity_clock_v2_alarm_count < productivity_clock_v2_alarm_capacity) {
        const uint32_t start = position;
        while (position < size && data[position] != '\n') ++position;
        const uint32_t end = position;
        if (position < size) ++position;
        if (end == start) continue;
        ProductivityClockAlarmV2 alarm{};
        if (!productivity_clock_v2_parse_alarm_line(data + start, end - start, alarm)) {
            serial::line("UI_CLOCK_V2_ALARM_REJECTED");
            continue;
        }
        productivity_clock_v2_alarms[productivity_clock_v2_alarm_count++] = alarm;
    }
    if (productivity_clock_v2_alarm_focus >= productivity_clock_v2_alarm_count) productivity_clock_v2_alarm_focus = 0U;
    serial::line("UI_CLOCK_V2_ALARMS_LOAD_OK");
}

bool productivity_clock_v2_parse_alarm_input(const char* input, uint8_t input_size, ProductivityClockAlarmV2& alarm) {
    memset(&alarm, 0, sizeof(alarm));
    if (!input || input_size < 7U || input_size >= productivity_clock_v2_input_capacity) return false;
    char text[productivity_clock_v2_input_capacity]{};
    memcpy(text, input, input_size);
    uint8_t size = input_size;
    text[size] = 0;
    productivity_calendar_clock_v2_trim(text, size);
    if (size < 7U) return false;

    alarm.day_mask = 127U;
    struct MaskSuffix { const char* text; uint8_t mask; };
    constexpr MaskSuffix suffixes[] = {
        {" !daily", 127U}, {" !weekdays", 62U}, {" !weekends", 65U}, {" !once", 0U},
    };
    for (const MaskSuffix& suffix : suffixes) {
        if (!productivity_calendar_clock_v2_suffix(text, size, suffix.text)) continue;
        size = static_cast<uint8_t>(size - string_length(suffix.text));
        text[size] = 0;
        alarm.day_mask = suffix.mask;
        productivity_calendar_clock_v2_trim(text, size);
        break;
    }

    uint32_t hour = 0U, minute = 0U;
    if (size < 7U || text[2] != ':' || text[5] != ' ' ||
        !productivity_calendar_clock_v2_parse_digits(text, 0U, 2U, hour) ||
        !productivity_calendar_clock_v2_parse_digits(text, 3U, 2U, minute) || hour > 23U || minute > 59U) return false;
    uint8_t title_start = 6U;
    while (title_start < size && text[title_start] == ' ') ++title_start;
    const uint8_t title_size = static_cast<uint8_t>(size - title_start);
    if (!productivity_calendar_clock_v2_title_valid(text + title_start, title_size, sizeof(alarm.title))) return false;
    string_copy(alarm.title, text + title_start, sizeof(alarm.title));
    alarm.hour = static_cast<uint8_t>(hour);
    alarm.minute = static_cast<uint8_t>(minute);
    alarm.enabled = true;
    alarm.last_fired_stamp = 0xFFFFFFFFU;
    return true;
}

const char* productivity_clock_v2_mask_text(uint8_t mask) {
    if (mask == 127U) return "DAILY";
    if (mask == 62U) return "WEEKDAYS";
    if (mask == 65U) return "WEEKENDS";
    return "ONCE";
}

void productivity_clock_v2_format_alarm_input(const ProductivityClockAlarmV2& alarm) {
    productivity_clock_v2_input_size = 0U;
    productivity_clock_v2_input[0] = 0;
    uint32_t length = 0U;
    productivity_append_uint(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, alarm.hour, 2U);
    (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, ":");
    productivity_append_uint(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, alarm.minute, 2U);
    (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, " ");
    (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, alarm.title);
    if (alarm.day_mask == 62U) (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, " !weekdays");
    else if (alarm.day_mask == 65U) (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, " !weekends");
    else if (alarm.day_mask == 0U) (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, " !once");
    else (void)productivity_append_text(productivity_clock_v2_input, sizeof(productivity_clock_v2_input), length, " !daily");
    productivity_clock_v2_input_size = static_cast<uint8_t>(length);
}

uint32_t productivity_clock_v2_timer_remaining(uint8_t index) {
    if (index >= productivity_clock_v2_timer_capacity) return 0U;
    ProductivityClockTimerV2& timer = productivity_clock_v2_timers[index];
    if (!timer.running) return timer.seconds;
    const uint32_t elapsed = static_cast<uint32_t>(timer_ticks - timer.started_tick) / 100U;
    if (elapsed >= timer.seconds) {
        timer.running = false;
        timer.seconds = 0U;
        if (!timer.expired_reported) {
            timer.expired_reported = true;
            productivity_clock_v2_banner_index = -2;
            productivity_clock_v2_banner_until = timer_ticks + 1200U;
            serial::line("UI_CLOCK_V2_TIMER_EXPIRED");
        }
        return 0U;
    }
    return timer.seconds - elapsed;
}

void productivity_clock_v2_toggle_timer() {
    ProductivityClockTimerV2& timer = productivity_clock_v2_timers[productivity_clock_v2_timer_focus];
    if (timer.running) {
        timer.seconds = productivity_clock_v2_timer_remaining(productivity_clock_v2_timer_focus);
        timer.running = false;
        serial::line("UI_CLOCK_V2_TIMER_PAUSED");
    } else if (timer.seconds) {
        timer.started_tick = timer_ticks;
        timer.running = true;
        timer.expired_reported = false;
        serial::line("UI_CLOCK_V2_TIMER_RUNNING");
    }
}

void productivity_clock_v2_add_minute() {
    ProductivityClockTimerV2& timer = productivity_clock_v2_timers[productivity_clock_v2_timer_focus];
    const uint32_t remaining = productivity_clock_v2_timer_remaining(productivity_clock_v2_timer_focus);
    timer.running = false;
    timer.seconds = remaining > 3540U ? 3600U : remaining + 60U;
    timer.expired_reported = false;
    serial::line("UI_CLOCK_V2_TIMER_SET_OK");
}

void productivity_clock_v2_add_lap() {
    const uint32_t ticks = productivity_stopwatch_ticks();
    if (!ticks || productivity_clock_v2_lap_count >= productivity_clock_v2_lap_capacity) return;
    productivity_clock_v2_laps[productivity_clock_v2_lap_count++] = ticks;
    serial::line("UI_CLOCK_V2_LAP_OK");
}

void productivity_clock_v2_reset_stopwatch() {
    productivity_stopwatch_running = false;
    productivity_stopwatch_accumulated = 0U;
    productivity_clock_v2_lap_count = 0U;
    memset(productivity_clock_v2_laps, 0, sizeof(productivity_clock_v2_laps));
    serial::line("UI_CLOCK_V2_STOPWATCH_RESET");
}

void productivity_clock_v2_begin_alarm_add() {
    if (productivity_clock_v2_alarm_count >= productivity_clock_v2_alarm_capacity) {
        serial::line("UI_CLOCK_V2_ALARM_CAPACITY");
        return;
    }
    productivity_clock_v2_alarm_editing = true;
    productivity_clock_v2_alarm_edit_existing = false;
    productivity_clock_v2_input_size = 0U;
    productivity_clock_v2_input[0] = 0;
}

void productivity_clock_v2_begin_alarm_edit() {
    if (!productivity_clock_v2_alarm_count || productivity_clock_v2_alarm_focus >= productivity_clock_v2_alarm_count) return;
    productivity_clock_v2_alarm_editing = true;
    productivity_clock_v2_alarm_edit_existing = true;
    productivity_clock_v2_format_alarm_input(productivity_clock_v2_alarms[productivity_clock_v2_alarm_focus]);
}

bool productivity_clock_v2_commit_alarm() {
    ProductivityClockAlarmV2 parsed{};
    if (!productivity_clock_v2_parse_alarm_input(productivity_clock_v2_input, productivity_clock_v2_input_size, parsed)) {
        serial::line("UI_CLOCK_V2_ALARM_INPUT_REJECTED");
        return false;
    }
    if (productivity_clock_v2_alarm_edit_existing) {
        if (!productivity_clock_v2_alarm_count || productivity_clock_v2_alarm_focus >= productivity_clock_v2_alarm_count) return false;
        productivity_clock_v2_alarms[productivity_clock_v2_alarm_focus] = parsed;
        serial::line("UI_CLOCK_V2_ALARM_EDIT_OK");
    } else {
        if (productivity_clock_v2_alarm_count >= productivity_clock_v2_alarm_capacity) return false;
        productivity_clock_v2_alarms[productivity_clock_v2_alarm_count++] = parsed;
        productivity_clock_v2_alarm_focus = static_cast<uint8_t>(productivity_clock_v2_alarm_count - 1U);
        serial::line("UI_CLOCK_V2_ALARM_ADD_OK");
    }
    productivity_clock_v2_alarm_editing = false;
    productivity_clock_v2_alarm_edit_existing = false;
    productivity_clock_v2_input_size = 0U;
    productivity_clock_v2_input[0] = 0;
    return productivity_clock_v2_save_alarms();
}

bool productivity_clock_v2_delete_alarm() {
    if (!productivity_clock_v2_alarm_count || productivity_clock_v2_alarm_focus >= productivity_clock_v2_alarm_count) return false;
    const uint8_t removed = productivity_clock_v2_alarm_focus;
    for (uint8_t index = static_cast<uint8_t>(removed + 1U); index < productivity_clock_v2_alarm_count; ++index)
        productivity_clock_v2_alarms[index - 1U] = productivity_clock_v2_alarms[index];
    --productivity_clock_v2_alarm_count;
    if (productivity_clock_v2_alarm_focus >= productivity_clock_v2_alarm_count) productivity_clock_v2_alarm_focus = 0U;
    const int32_t removed_index = static_cast<int32_t>(removed);
    if (productivity_clock_v2_banner_index == removed_index) productivity_clock_v2_banner_index = -1;
    else if (productivity_clock_v2_banner_index > removed_index) --productivity_clock_v2_banner_index;
    const bool saved = productivity_clock_v2_save_alarms();
    serial::line(saved ? "UI_CLOCK_V2_ALARM_DELETE_OK" : "UI_CLOCK_V2_ALARM_DELETE_FAILED");
    return saved;
}

bool productivity_clock_v2_background_tick() {
    bool changed = false;
    for (uint8_t index = 0U; index < productivity_clock_v2_timer_capacity; ++index) {
        const bool was_running = productivity_clock_v2_timers[index].running;
        (void)productivity_clock_v2_timer_remaining(index);
        if (was_running && !productivity_clock_v2_timers[index].running) changed = true;
    }
    if (productivity_clock_v2_banner_index != -1 && static_cast<int32_t>(timer_ticks - productivity_clock_v2_banner_until) >= 0) {
        productivity_clock_v2_banner_index = -1;
        changed = true;
    }

    uint8_t hour = 0U, minute = 0U, weekday = 0U, day = 0U, month = 0U, year = 0U;
    if (!rtc_read_time(hour, minute) || !rtc_read_date(weekday, day, month, year) || weekday > 6U) return changed;
    const uint32_t stamp = productivity_calendar_v2_ordinal(static_cast<uint16_t>(2000U + year), month, day) * 1440U + static_cast<uint32_t>(hour) * 60U + minute;
    for (uint8_t index = 0U; index < productivity_clock_v2_alarm_count; ++index) {
        ProductivityClockAlarmV2& alarm = productivity_clock_v2_alarms[index];
        if (!alarm.enabled || alarm.hour != hour || alarm.minute != minute || alarm.last_fired_stamp == stamp) continue;
        if (alarm.day_mask && (alarm.day_mask & static_cast<uint8_t>(1U << weekday)) == 0U) continue;
        alarm.last_fired_stamp = stamp;
        productivity_clock_v2_banner_index = static_cast<int32_t>(index);
        productivity_clock_v2_banner_until = timer_ticks + 1200U;
        serial::line("UI_CLOCK_V2_ALARM_DUE");
        changed = true;
        if (!alarm.day_mask) {
            alarm.enabled = false;
            (void)productivity_clock_v2_save_alarms();
        }
        break;
    }
    return changed;
}

void productivity_clock_v2_render_banner() {
    if (productivity_clock_v2_banner_index == -1) return;
    const int32_t x = static_cast<int32_t>(width / 2U) - 170;
    const int32_t y = 52;
    fill_round_rect(x, y, 340, 54, 10, current_theme.surface_high);
    if (productivity_clock_v2_banner_index == -2) {
        draw_text(x + 16, y + 16, "TIMER COMPLETE", current_theme.warning, 1);
        draw_text(x + 16, y + 34, "ZEN CLOCK", current_theme.text_muted, 1);
        return;
    }
    if (productivity_clock_v2_banner_index < 0 || productivity_clock_v2_banner_index >= productivity_clock_v2_alarm_count) return;
    const uint8_t index = static_cast<uint8_t>(productivity_clock_v2_banner_index);
    const ProductivityClockAlarmV2& alarm = productivity_clock_v2_alarms[index];
    draw_text(x + 16, y + 14, "ALARM", current_theme.warning, 1);
    draw_two_digits(x + 68, y + 14, alarm.hour, current_theme.accent);
    draw_char(x + 82, y + 14, ':', current_theme.text_muted, 1);
    draw_two_digits(x + 90, y + 14, alarm.minute, current_theme.accent);
    draw_text(x + 16, y + 34, alarm.title, current_theme.text, 1);
    draw_text_right(x + 324, y + 34, productivity_clock_v2_mask_text(alarm.day_mask), current_theme.text_muted, 1);
}
