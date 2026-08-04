const char* productivity_reminder_repeat_text(ProductivityReminderRepeat repeat) {
    if (repeat == ProductivityReminderRepeat::daily) return "DAILY";
    if (repeat == ProductivityReminderRepeat::weekly) return "WEEKLY";
    if (repeat == ProductivityReminderRepeat::monthly) return "MONTHLY";
    return "ONCE";
}

char productivity_reminder_repeat_code(ProductivityReminderRepeat repeat) {
    if (repeat == ProductivityReminderRepeat::daily) return 'D';
    if (repeat == ProductivityReminderRepeat::weekly) return 'W';
    if (repeat == ProductivityReminderRepeat::monthly) return 'M';
    return 'N';
}

bool productivity_reminder_repeat_from_code(char code, ProductivityReminderRepeat& repeat) {
    if (code == 'N') repeat = ProductivityReminderRepeat::none;
    else if (code == 'D') repeat = ProductivityReminderRepeat::daily;
    else if (code == 'W') repeat = ProductivityReminderRepeat::weekly;
    else if (code == 'M') repeat = ProductivityReminderRepeat::monthly;
    else return false;
    return true;
}

ProductivityReminderRepeat productivity_reminder_repeat_next(ProductivityReminderRepeat repeat) {
    if (repeat == ProductivityReminderRepeat::none) return ProductivityReminderRepeat::daily;
    if (repeat == ProductivityReminderRepeat::daily) return ProductivityReminderRepeat::weekly;
    if (repeat == ProductivityReminderRepeat::weekly) return ProductivityReminderRepeat::monthly;
    return ProductivityReminderRepeat::none;
}

bool productivity_reminder_title_valid(const char* title, uint8_t size) {
    if (!title || !size || size >= sizeof(productivity_reminders[0].title)) return false;
    for (uint8_t index = 0U; index < size; ++index) {
        const char value = title[index];
        if (value < 32 || value > 126 || value == '|') return false;
    }
    return true;
}

bool productivity_reminder_parse_digits(const char* text, uint32_t offset, uint8_t count, uint32_t& value) {
    value = 0U;
    if (!text) return false;
    for (uint8_t index = 0U; index < count; ++index) {
        const char digit = text[offset + index];
        if (digit < '0' || digit > '9') return false;
        value = value * 10U + static_cast<uint32_t>(digit - '0');
    }
    return true;
}

bool productivity_reminder_datetime_valid(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute) {
    return year >= 2000U && year <= 2099U && month >= 1U && month <= 12U && day >= 1U &&
        day <= productivity_days_in_month(static_cast<uint16_t>(year), static_cast<uint8_t>(month)) &&
        hour <= 23U && minute <= 59U;
}

bool productivity_reminder_parse_line(const char* line, uint32_t size, ProductivityReminder& output) {
    memset(&output, 0, sizeof(output));
    if (!line || size < 23U || size > 67U || line[0] != 'R' || line[2] != '|' ||
        (line[3] != '0' && line[3] != '1') || line[4] != '|' ||
        line[9] != '-' || line[12] != '-' || line[15] != '|' ||
        line[18] != ':' || line[21] != '|') return false;

    uint32_t title_offset = 22U;
    output.repeat = ProductivityReminderRepeat::none;
    output.repeat_day = 0U;
    if (line[1] == '2') {
        uint32_t repeat_day = 0U;
        if (size < 28U || line[23] != '|' || line[26] != '|' ||
            !productivity_reminder_repeat_from_code(line[22], output.repeat) ||
            !productivity_reminder_parse_digits(line, 24U, 2U, repeat_day) || repeat_day < 1U || repeat_day > 31U) return false;
        output.repeat_day = static_cast<uint8_t>(repeat_day);
        title_offset = 27U;
    } else if (line[1] != '1') return false;

    uint32_t year = 0U, month = 0U, day = 0U, hour = 0U, minute = 0U;
    if (!productivity_reminder_parse_digits(line, 5U, 4U, year) ||
        !productivity_reminder_parse_digits(line, 10U, 2U, month) ||
        !productivity_reminder_parse_digits(line, 13U, 2U, day) ||
        !productivity_reminder_parse_digits(line, 16U, 2U, hour) ||
        !productivity_reminder_parse_digits(line, 19U, 2U, minute) ||
        !productivity_reminder_datetime_valid(year, month, day, hour, minute)) return false;

    const uint32_t title_size = size - title_offset;
    if (!title_size || title_size >= sizeof(output.title)) return false;
    for (uint32_t index = 0U; index < title_size; ++index) {
        const char value = line[title_offset + index];
        if (value < 32 || value > 126 || value == '|') return false;
        output.title[index] = value;
    }
    output.title[title_size] = 0;
    output.year = static_cast<uint16_t>(year);
    output.month = static_cast<uint8_t>(month);
    output.day = static_cast<uint8_t>(day);
    if (!output.repeat_day) output.repeat_day = output.day;
    output.hour = static_cast<uint8_t>(hour);
    output.minute = static_cast<uint8_t>(minute);
    output.done = line[3] == '1';
    output.notified = false;
    return true;
}

bool productivity_reminder_suffix_matches(const char* text, uint8_t size, const char* suffix) {
    const uint32_t suffix_size = string_length(suffix);
    if (!text || !suffix || suffix_size > size) return false;
    const uint32_t start = static_cast<uint32_t>(size) - suffix_size;
    for (uint32_t index = 0U; index < suffix_size; ++index)
        if (text[start + index] != suffix[index]) return false;
    return true;
}

void productivity_reminder_trim(char* text, uint8_t& size) {
    if (!text) return;
    uint8_t start = 0U;
    while (start < size && text[start] == ' ') ++start;
    if (start) {
        for (uint8_t index = start; index < size; ++index) text[index - start] = text[index];
        size = static_cast<uint8_t>(size - start);
    }
    while (size && text[size - 1U] == ' ') --size;
    text[size] = 0;
}

bool productivity_reminder_parse_quick(const char* input, uint8_t input_size, ProductivityReminderRepeat default_repeat,
                                       ProductivityReminder& output) {
    memset(&output, 0, sizeof(output));
    if (!input || !input_size || input_size >= productivity_reminder_input_capacity) return false;
    char text[productivity_reminder_input_capacity]{};
    for (uint8_t index = 0U; index < input_size; ++index) text[index] = input[index];
    uint8_t size = input_size;
    text[size] = 0;
    productivity_reminder_trim(text, size);
    if (!size) return false;

    output.repeat = default_repeat;
    struct RepeatSuffix { const char* text; ProductivityReminderRepeat repeat; };
    constexpr RepeatSuffix suffixes[] = {
        {" !daily", ProductivityReminderRepeat::daily},
        {" !weekly", ProductivityReminderRepeat::weekly},
        {" !monthly", ProductivityReminderRepeat::monthly},
        {" !once", ProductivityReminderRepeat::none},
    };
    for (const RepeatSuffix& suffix : suffixes) {
        if (!productivity_reminder_suffix_matches(text, size, suffix.text)) continue;
        size = static_cast<uint8_t>(size - string_length(suffix.text));
        text[size] = 0;
        output.repeat = suffix.repeat;
        productivity_reminder_trim(text, size);
        break;
    }

    uint16_t year = 2026U;
    uint8_t month = 1U, day = 1U, hour = 0U, minute = 0U;
    if (!productivity_utility_now(year, month, day, hour, minute)) return false;
    productivity_utility_add_minutes(year, month, day, hour, minute, 10);

    int32_t schedule_start = -1;
    for (uint8_t index = 0U; index + 1U < size; ++index)
        if (text[index] == ' ' && text[index + 1U] == '@') schedule_start = static_cast<int32_t>(index);
    if (schedule_start >= 0) {
        const uint8_t marker = static_cast<uint8_t>(schedule_start + 2);
        if (marker >= size) return false;
        if (text[marker] == '+') {
            uint32_t value = 0U;
            uint8_t position = static_cast<uint8_t>(marker + 1U);
            const uint8_t digits_start = position;
            while (position < size && text[position] >= '0' && text[position] <= '9') {
                const uint32_t digit = static_cast<uint32_t>(text[position] - '0');
                if (value > (525600U - digit) / 10U) return false;
                value = value * 10U + digit;
                ++position;
            }
            if (position == digits_start || position + 1U != size || !value) return false;
            uint32_t multiplier = 0U;
            if (text[position] == 'm') multiplier = 1U;
            else if (text[position] == 'h') multiplier = 60U;
            else if (text[position] == 'd') multiplier = 1440U;
            else return false;
            if (value > 525600U / multiplier) return false;
            if (!productivity_utility_now(year, month, day, hour, minute)) return false;
            productivity_utility_add_minutes(year, month, day, hour, minute, static_cast<int32_t>(value * multiplier));
        } else {
            if (static_cast<uint32_t>(size - marker) != 16U || text[marker + 4U] != '-' || text[marker + 7U] != '-' ||
                text[marker + 10U] != ' ' || text[marker + 13U] != ':') return false;
            uint32_t parsed_year = 0U, parsed_month = 0U, parsed_day = 0U, parsed_hour = 0U, parsed_minute = 0U;
            if (!productivity_reminder_parse_digits(text, marker, 4U, parsed_year) ||
                !productivity_reminder_parse_digits(text, marker + 5U, 2U, parsed_month) ||
                !productivity_reminder_parse_digits(text, marker + 8U, 2U, parsed_day) ||
                !productivity_reminder_parse_digits(text, marker + 11U, 2U, parsed_hour) ||
                !productivity_reminder_parse_digits(text, marker + 14U, 2U, parsed_minute) ||
                !productivity_reminder_datetime_valid(parsed_year, parsed_month, parsed_day, parsed_hour, parsed_minute)) return false;
            year = static_cast<uint16_t>(parsed_year);
            month = static_cast<uint8_t>(parsed_month);
            day = static_cast<uint8_t>(parsed_day);
            hour = static_cast<uint8_t>(parsed_hour);
            minute = static_cast<uint8_t>(parsed_minute);
        }
        size = static_cast<uint8_t>(schedule_start);
        text[size] = 0;
        productivity_reminder_trim(text, size);
    }

    if (!productivity_reminder_title_valid(text, size)) return false;
    string_copy(output.title, text, sizeof(output.title));
    output.year = year;
    output.month = month;
    output.day = day;
    output.repeat_day = day;
    output.hour = hour;
    output.minute = minute;
    output.done = false;
    output.notified = false;
    return true;
}

bool productivity_reminders_save() {
    if (!productivity_utilities_ensure_storage()) return false;
    char data[2304]{};
    uint32_t length = 0U;
    for (uint8_t index = 0U; index < productivity_reminder_count; ++index) {
        const ProductivityReminder& reminder = productivity_reminders[index];
        if (!productivity_reminder_title_valid(reminder.title, static_cast<uint8_t>(string_length(reminder.title)))) return false;
        if (!productivity_append_text(data, sizeof(data), length, "R2|")) return false;
        if (length + 2U >= sizeof(data)) return false;
        data[length++] = reminder.done ? '1' : '0';
        data[length++] = '|';
        data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, reminder.year, 4U);
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = '-'; data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, reminder.month, 2U);
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = '-'; data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, reminder.day, 2U);
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = '|'; data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, reminder.hour, 2U);
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = ':'; data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, reminder.minute, 2U);
        if (length + 6U >= sizeof(data)) return false;
        data[length++] = '|';
        data[length++] = productivity_reminder_repeat_code(reminder.repeat);
        data[length++] = '|';
        data[length] = 0;
        productivity_append_uint(data, sizeof(data), length, reminder.repeat_day ? reminder.repeat_day : reminder.day, 2U);
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = '|';
        data[length] = 0;
        if (!productivity_append_text(data, sizeof(data), length, reminder.title)) return false;
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = '\n'; data[length] = 0;
    }
    const bool saved = storage::guarded_write_file("/reminders/reminders.db", reinterpret_cast<const uint8_t*>(data), length, false);
    serial::line(saved ? "UI_REMINDERS_SAVE_OK" : "UI_REMINDERS_SAVE_FAILED");
    return saved;
}

void productivity_reminders_load() {
    productivity_reminder_count = 0U;
    memset(productivity_reminders, 0, sizeof(productivity_reminders));
    if (!productivity_utilities_ensure_storage()) return;
    char data[2304]{};
    uint32_t size = 0U;
    if (!storage::security_read_file("/reminders/reminders.db", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
        serial::line("UI_REMINDERS_LOAD_EMPTY");
        return;
    }
    data[size] = 0;
    uint32_t position = 0U;
    while (position < size && productivity_reminder_count < productivity_reminder_capacity) {
        const uint32_t start = position;
        while (position < size && data[position] != '\n') ++position;
        const uint32_t end = position;
        if (position < size) ++position;
        if (end == start) continue;
        ProductivityReminder reminder{};
        if (!productivity_reminder_parse_line(data + start, end - start, reminder)) {
            serial::line("UI_REMINDERS_RECORD_REJECTED");
            continue;
        }
        productivity_reminders[productivity_reminder_count++] = reminder;
    }
    serial::line("UI_REMINDERS_LOAD_OK");
}
