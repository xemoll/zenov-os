bool productivity_calendar_v2_parse_line(const char* line, uint32_t size, ProductivityCalendarEventV2& event) {
    memset(&event, 0, sizeof(event));
    if (!line || size < 31U || size > 70U || line[0] != 'E' || line[1] != '2' || line[2] != '|' ||
        line[7] != '-' || line[10] != '-' || line[13] != '|' || line[16] != ':' || line[19] != '|' ||
        line[24] != '|' || line[26] != '|' || line[29] != '|') return false;
    uint32_t year = 0U, month = 0U, day = 0U, hour = 0U, minute = 0U, duration = 0U, anchor = 0U;
    if (!productivity_calendar_clock_v2_parse_digits(line, 3U, 4U, year) ||
        !productivity_calendar_clock_v2_parse_digits(line, 8U, 2U, month) ||
        !productivity_calendar_clock_v2_parse_digits(line, 11U, 2U, day) ||
        !productivity_calendar_clock_v2_parse_digits(line, 14U, 2U, hour) ||
        !productivity_calendar_clock_v2_parse_digits(line, 17U, 2U, minute) ||
        !productivity_calendar_clock_v2_parse_digits(line, 20U, 4U, duration) ||
        !productivity_calendar_clock_v2_parse_digits(line, 27U, 2U, anchor) ||
        !productivity_calendar_v2_date_valid(year, month, day) || hour > 23U || minute > 59U ||
        duration < 1U || duration > 1440U || anchor < 1U || anchor > 31U ||
        !productivity_calendar_v2_repeat_from_code(line[25], event.repeat)) return false;
    const uint32_t title_size = size - 30U;
    if (!title_size || title_size >= sizeof(event.title)) return false;
    for (uint32_t index = 0U; index < title_size; ++index) {
        const char value = line[30U + index];
        if (value < 32 || value > 126 || value == '|') return false;
        event.title[index] = value;
    }
    event.title[title_size] = 0;
    event.year = static_cast<uint16_t>(year);
    event.month = static_cast<uint8_t>(month);
    event.day = static_cast<uint8_t>(day);
    event.hour = static_cast<uint8_t>(hour);
    event.minute = static_cast<uint8_t>(minute);
    event.duration_minutes = static_cast<uint16_t>(duration);
    event.repeat_day = static_cast<uint8_t>(anchor);
    return true;
}

bool productivity_calendar_v2_before(const ProductivityCalendarEventV2& left, const ProductivityCalendarEventV2& right) {
    const uint32_t left_day = productivity_calendar_v2_ordinal(left.year, left.month, left.day);
    const uint32_t right_day = productivity_calendar_v2_ordinal(right.year, right.month, right.day);
    if (left_day != right_day) return left_day < right_day;
    const uint32_t left_time = static_cast<uint32_t>(left.hour) * 60U + left.minute;
    const uint32_t right_time = static_cast<uint32_t>(right.hour) * 60U + right.minute;
    if (left_time != right_time) return left_time < right_time;
    return productivity_task_compare_text(left.title, right.title) < 0;
}

void productivity_calendar_v2_sort() {
    for (uint8_t index = 1U; index < productivity_calendar_v2_count; ++index) {
        const ProductivityCalendarEventV2 value = productivity_calendar_v2_events[index];
        uint8_t position = index;
        while (position > 0U && productivity_calendar_v2_before(value, productivity_calendar_v2_events[position - 1U])) {
            productivity_calendar_v2_events[position] = productivity_calendar_v2_events[position - 1U];
            --position;
        }
        productivity_calendar_v2_events[position] = value;
    }
}

void productivity_calendar_v2_rebuild_day() {
    productivity_calendar_v2_day_count = 0U;
    for (uint8_t index = 0U; index < productivity_calendar_v2_count; ++index) {
        if (productivity_calendar_v2_occurs_on(productivity_calendar_v2_events[index], productivity_calendar_year,
                                               productivity_calendar_month, productivity_calendar_day))
            productivity_calendar_v2_day_indices[productivity_calendar_v2_day_count++] = index;
    }
    if (!productivity_calendar_v2_day_count) productivity_calendar_v2_focus = 0U;
    else if (productivity_calendar_v2_focus >= productivity_calendar_v2_day_count)
        productivity_calendar_v2_focus = static_cast<uint8_t>(productivity_calendar_v2_day_count - 1U);
}

bool productivity_calendar_v2_save_projection() {
    char data[1024]{};
    uint32_t length = 0U;
    uint8_t weekday = 0U, now_day = 1U, now_month = 1U, now_year = 0U;
    if (!rtc_read_date(weekday, now_day, now_month, now_year)) {
        now_year = static_cast<uint8_t>(productivity_calendar_year - 2000U);
        now_month = productivity_calendar_month;
        now_day = productivity_calendar_day;
    }
    productivity_event_count = 0U;
    for (uint8_t index = 0U; index < productivity_calendar_v2_count && productivity_event_count < productivity_event_capacity; ++index) {
        const ProductivityCalendarEventV2& source = productivity_calendar_v2_events[index];
        uint16_t year = 0U; uint8_t month = 0U, day = 0U;
        if (!productivity_calendar_v2_next_occurrence(source, static_cast<uint16_t>(2000U + now_year), now_month, now_day, year, month, day)) continue;
        ProductivityEvent& projection = productivity_events[productivity_event_count++];
        projection.year = year;
        projection.month = month;
        projection.day = day;
        string_copy(projection.title, source.title, sizeof(projection.title));
        char date[11]{};
        productivity_date_text(date, year, month, day);
        if (!productivity_append_text(data, sizeof(data), length, date) ||
            !productivity_append_text(data, sizeof(data), length, "|") ||
            !productivity_append_text(data, sizeof(data), length, projection.title) ||
            !productivity_append_text(data, sizeof(data), length, "\n")) return false;
    }
    return storage::guarded_write_file("/calendar/events.db", reinterpret_cast<const uint8_t*>(data), length, false);
}

bool productivity_calendar_v2_save() {
    char data[2048]{};
    uint32_t length = 0U;
    for (uint8_t index = 0U; index < productivity_calendar_v2_count; ++index) {
        const ProductivityCalendarEventV2& event = productivity_calendar_v2_events[index];
        const uint8_t title_size = static_cast<uint8_t>(string_length(event.title));
        if (!productivity_calendar_clock_v2_title_valid(event.title, title_size, sizeof(event.title))) return false;
        if (!productivity_append_text(data, sizeof(data), length, "E2|")) return false;
        productivity_append_uint(data, sizeof(data), length, event.year, 4U);
        if (!productivity_append_text(data, sizeof(data), length, "-")) return false;
        productivity_append_uint(data, sizeof(data), length, event.month, 2U);
        if (!productivity_append_text(data, sizeof(data), length, "-")) return false;
        productivity_append_uint(data, sizeof(data), length, event.day, 2U);
        if (!productivity_append_text(data, sizeof(data), length, "|")) return false;
        productivity_append_uint(data, sizeof(data), length, event.hour, 2U);
        if (!productivity_append_text(data, sizeof(data), length, ":")) return false;
        productivity_append_uint(data, sizeof(data), length, event.minute, 2U);
        if (!productivity_append_text(data, sizeof(data), length, "|")) return false;
        productivity_append_uint(data, sizeof(data), length, event.duration_minutes, 4U);
        if (length + 3U >= sizeof(data)) return false;
        data[length++] = '|';
        data[length++] = productivity_calendar_v2_repeat_code(event.repeat);
        data[length++] = '|';
        data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, event.repeat_day ? event.repeat_day : event.day, 2U);
        if (!productivity_append_text(data, sizeof(data), length, "|") ||
            !productivity_append_text(data, sizeof(data), length, event.title) ||
            !productivity_append_text(data, sizeof(data), length, "\n")) return false;
    }
    const bool source_saved = storage::guarded_write_file("/calendar/events-v2.db", reinterpret_cast<const uint8_t*>(data), length, false);
    const bool projection_saved = source_saved && productivity_calendar_v2_save_projection();
    serial::line(projection_saved ? "UI_CALENDAR_V2_SAVE_OK" : "UI_CALENDAR_V2_SAVE_FAILED");
    return projection_saved;
}

void productivity_calendar_v2_migrate_legacy() {
    productivity_calendar_load();
    productivity_calendar_v2_count = 0U;
    for (uint8_t index = 0U; index < productivity_event_count && index < productivity_calendar_v2_capacity; ++index) {
        const ProductivityEvent& legacy = productivity_events[index];
        ProductivityCalendarEventV2& event = productivity_calendar_v2_events[productivity_calendar_v2_count++];
        memset(&event, 0, sizeof(event));
        event.year = legacy.year;
        event.month = legacy.month;
        event.day = legacy.day;
        event.hour = 9U;
        event.minute = 0U;
        event.duration_minutes = 60U;
        event.repeat = ProductivityCalendarRepeat::none;
        event.repeat_day = legacy.day;
        string_copy(event.title, legacy.title, sizeof(event.title));
    }
    if (productivity_calendar_v2_count) {
        productivity_calendar_v2_sort();
        (void)productivity_calendar_v2_save();
        serial::line("UI_CALENDAR_V2_MIGRATE_OK");
    }
}

void productivity_calendar_v2_load() {
    productivity_calendar_v2_count = 0U;
    memset(productivity_calendar_v2_events, 0, sizeof(productivity_calendar_v2_events));
    char data[2048]{};
    uint32_t size = 0U;
    if (!storage::security_read_file("/calendar/events-v2.db", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
        productivity_calendar_v2_migrate_legacy();
        productivity_calendar_v2_rebuild_day();
        serial::line("UI_CALENDAR_V2_LOAD_LEGACY");
        return;
    }
    data[size] = 0;
    uint32_t position = 0U;
    while (position < size && productivity_calendar_v2_count < productivity_calendar_v2_capacity) {
        const uint32_t start = position;
        while (position < size && data[position] != '\n') ++position;
        const uint32_t end = position;
        if (position < size) ++position;
        if (end == start) continue;
        ProductivityCalendarEventV2 event{};
        if (!productivity_calendar_v2_parse_line(data + start, end - start, event)) {
            serial::line("UI_CALENDAR_V2_RECORD_REJECTED");
            continue;
        }
        productivity_calendar_v2_events[productivity_calendar_v2_count++] = event;
    }
    productivity_calendar_v2_sort();
    productivity_calendar_v2_rebuild_day();
    (void)productivity_calendar_v2_save_projection();
    serial::line("UI_CALENDAR_V2_LOAD_OK");
}

bool productivity_calendar_v2_parse_input(const char* input, uint8_t input_size, ProductivityCalendarEventV2& event) {
    memset(&event, 0, sizeof(event));
    if (!input || !input_size || input_size >= productivity_calendar_v2_input_capacity) return false;
    char text[productivity_calendar_v2_input_capacity]{};
    memcpy(text, input, input_size);
    uint8_t size = input_size;
    text[size] = 0;
    productivity_calendar_clock_v2_trim(text, size);
    if (!size) return false;

    event.repeat = ProductivityCalendarRepeat::none;
    struct RepeatSuffix { const char* text; ProductivityCalendarRepeat repeat; };
    constexpr RepeatSuffix repeats[] = {
        {" !daily", ProductivityCalendarRepeat::daily},
        {" !weekly", ProductivityCalendarRepeat::weekly},
        {" !monthly", ProductivityCalendarRepeat::monthly},
        {" !once", ProductivityCalendarRepeat::none},
    };
    for (const RepeatSuffix& suffix : repeats) {
        if (!productivity_calendar_clock_v2_suffix(text, size, suffix.text)) continue;
        size = static_cast<uint8_t>(size - string_length(suffix.text));
        text[size] = 0;
        event.repeat = suffix.repeat;
        productivity_calendar_clock_v2_trim(text, size);
        break;
    }

    event.duration_minutes = 60U;
    int32_t duration_start = -1;
    for (uint8_t index = 0U; index + 1U < size; ++index)
        if (text[index] == ' ' && text[index + 1U] == '+') duration_start = static_cast<int32_t>(index);
    if (duration_start >= 0) {
        uint32_t duration = 0U;
        const uint8_t digits = static_cast<uint8_t>(duration_start + 2);
        uint8_t position = digits;
        while (position < size && text[position] >= '0' && text[position] <= '9') {
            if (duration > 1440U) return false;
            duration = duration * 10U + static_cast<uint32_t>(text[position] - '0');
            ++position;
        }
        if (position == digits || position != size || duration < 1U || duration > 1440U) return false;
        event.duration_minutes = static_cast<uint16_t>(duration);
        size = static_cast<uint8_t>(duration_start);
        text[size] = 0;
        productivity_calendar_clock_v2_trim(text, size);
    }

    event.hour = 9U;
    event.minute = 0U;
    int32_t time_start = -1;
    for (uint8_t index = 0U; index + 1U < size; ++index)
        if (text[index] == ' ' && text[index + 1U] == '@') time_start = static_cast<int32_t>(index);
    if (time_start >= 0) {
        const uint8_t offset = static_cast<uint8_t>(time_start + 2);
        uint32_t hour = 0U, minute = 0U;
        if (static_cast<uint32_t>(size - offset) != 5U || text[offset + 2U] != ':' ||
            !productivity_calendar_clock_v2_parse_digits(text, offset, 2U, hour) ||
            !productivity_calendar_clock_v2_parse_digits(text, offset + 3U, 2U, minute) || hour > 23U || minute > 59U) return false;
        event.hour = static_cast<uint8_t>(hour);
        event.minute = static_cast<uint8_t>(minute);
        size = static_cast<uint8_t>(time_start);
        text[size] = 0;
        productivity_calendar_clock_v2_trim(text, size);
    }

    if (!productivity_calendar_clock_v2_title_valid(text, size, sizeof(event.title))) return false;
    string_copy(event.title, text, sizeof(event.title));
    event.year = productivity_calendar_year;
    event.month = productivity_calendar_month;
    event.day = productivity_calendar_day;
    event.repeat_day = productivity_calendar_day;
    return true;
}

void productivity_calendar_v2_format_input(const ProductivityCalendarEventV2& event) {
    productivity_calendar_v2_input_size = 0U;
    productivity_calendar_v2_input[0] = 0;
    uint32_t length = 0U;
    (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, event.title);
    (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, " @");
    productivity_append_uint(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, event.hour, 2U);
    (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, ":");
    productivity_append_uint(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, event.minute, 2U);
    (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, " +");
    productivity_append_uint(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, event.duration_minutes);
    if (event.repeat == ProductivityCalendarRepeat::daily) (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, " !daily");
    else if (event.repeat == ProductivityCalendarRepeat::weekly) (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, " !weekly");
    else if (event.repeat == ProductivityCalendarRepeat::monthly) (void)productivity_append_text(productivity_calendar_v2_input, sizeof(productivity_calendar_v2_input), length, " !monthly");
    productivity_calendar_v2_input_size = static_cast<uint8_t>(length);
}
