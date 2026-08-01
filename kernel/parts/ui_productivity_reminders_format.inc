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

bool productivity_reminder_parse_line(const char* line, uint32_t size, ProductivityReminder& output) {
    memset(&output, 0, sizeof(output));
    if (!line || size < 23U || size > 62U) return false;
    if (line[0] != 'R' || line[1] != '1' || line[2] != '|' ||
        (line[3] != '0' && line[3] != '1') || line[4] != '|' ||
        line[9] != '-' || line[12] != '-' || line[15] != '|' ||
        line[18] != ':' || line[21] != '|') return false;
    uint32_t year = 0U, month = 0U, day = 0U, hour = 0U, minute = 0U;
    if (!productivity_reminder_parse_digits(line, 5U, 4U, year) ||
        !productivity_reminder_parse_digits(line, 10U, 2U, month) ||
        !productivity_reminder_parse_digits(line, 13U, 2U, day) ||
        !productivity_reminder_parse_digits(line, 16U, 2U, hour) ||
        !productivity_reminder_parse_digits(line, 19U, 2U, minute)) return false;
    if (year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U ||
        day > productivity_days_in_month(static_cast<uint16_t>(year), static_cast<uint8_t>(month)) ||
        hour > 23U || minute > 59U) return false;
    const uint32_t title_size = size - 22U;
    if (!title_size || title_size >= sizeof(output.title)) return false;
    for (uint32_t index = 0U; index < title_size; ++index) {
        const char value = line[22U + index];
        if (value < 32 || value > 126 || value == '|') return false;
        output.title[index] = value;
    }
    output.title[title_size] = 0;
    output.year = static_cast<uint16_t>(year);
    output.month = static_cast<uint8_t>(month);
    output.day = static_cast<uint8_t>(day);
    output.hour = static_cast<uint8_t>(hour);
    output.minute = static_cast<uint8_t>(minute);
    output.done = line[3] == '1';
    output.notified = false;
    return true;
}

bool productivity_reminders_save() {
    if (!productivity_utilities_ensure_storage()) return false;
    char data[2048]{};
    uint32_t length = 0U;
    for (uint8_t index = 0U; index < productivity_reminder_count; ++index) {
        const ProductivityReminder& reminder = productivity_reminders[index];
        if (!productivity_reminder_title_valid(reminder.title, static_cast<uint8_t>(string_length(reminder.title)))) return false;
        if (!productivity_append_text(data, sizeof(data), length, "R1|")) return false;
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
        if (length + 1U >= sizeof(data)) return false;
        data[length++] = '|'; data[length] = 0;
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
    char data[2048]{};
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
