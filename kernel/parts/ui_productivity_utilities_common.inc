bool productivity_utilities_ensure_storage() {
    if (productivity_utilities_storage_ready) return true;
    (void)productivity_ensure_storage();
    char normalized[48]{};
    if (!storage::browser_directory_exists("/utilities", normalized)) (void)storage::make_directory("/utilities");
    if (!storage::browser_directory_exists("/reminders", normalized)) (void)storage::make_directory("/reminders");
    const bool ready = storage::browser_directory_exists("/utilities", normalized) &&
        storage::browser_directory_exists("/reminders", normalized);
    productivity_utilities_storage_ready = ready;
    serial::line(ready ? "UI_PRODUCTIVITY_UTILITIES_STORAGE_OK" : "UI_PRODUCTIVITY_UTILITIES_STORAGE_FAILED");
    return ready;
}

void productivity_utility_append_int32(char* output, uint32_t capacity, uint32_t& length, int32_t value) {
    if (!output || !capacity || length >= capacity) return;
    uint32_t magnitude = static_cast<uint32_t>(value);
    if (value < 0) {
        if (length + 1U < capacity) output[length++] = '-';
        magnitude = 0U - magnitude;
    }
    productivity_append_uint(output, capacity, length, magnitude);
}

void productivity_utility_format_int32(char* output, uint32_t capacity, int32_t value) {
    if (!output || !capacity) return;
    output[0] = 0;
    uint32_t length = 0U;
    productivity_utility_append_int32(output, capacity, length, value);
}

bool productivity_utility_parse_signed(const char* text, uint32_t size, int32_t& output) {
    output = 0;
    if (!text || !size) return false;
    uint32_t position = 0U;
    bool negative = false;
    if (text[position] == '-' || text[position] == '+') {
        negative = text[position] == '-';
        if (++position >= size) return false;
    }
    uint32_t magnitude = 0U;
    const uint32_t limit = negative ? 2147483648U : 2147483647U;
    for (; position < size; ++position) {
        if (text[position] < '0' || text[position] > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(text[position] - '0');
        if (magnitude > (limit - digit) / 10U) return false;
        magnitude = magnitude * 10U + digit;
    }
    output = negative ? static_cast<int32_t>(0U - magnitude) : static_cast<int32_t>(magnitude);
    return true;
}

bool productivity_utility_now(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) {
    uint8_t weekday = 0U, short_year = 0U;
    const bool date_ok = rtc_read_date(weekday, day, month, short_year);
    const bool time_ok = rtc_read_time(hour, minute);
    year = static_cast<uint16_t>(2000U + short_year);
    return date_ok && time_ok;
}

uint32_t productivity_utility_date_ordinal(uint16_t year, uint8_t month, uint8_t day) {
    uint32_t ordinal = 0U;
    for (uint16_t current = 2000U; current < year; ++current) ordinal += productivity_is_leap(current) ? 366U : 365U;
    for (uint8_t current = 1U; current < month; ++current) ordinal += productivity_days_in_month(year, current);
    return ordinal + static_cast<uint32_t>(day - 1U);
}

void productivity_utility_date_from_ordinal(uint32_t ordinal, uint16_t& year, uint8_t& month, uint8_t& day) {
    year = 2000U;
    while (year < 2099U) {
        const uint32_t year_days = productivity_is_leap(year) ? 366U : 365U;
        if (ordinal < year_days) break;
        ordinal -= year_days;
        ++year;
    }
    month = 1U;
    while (month < 12U) {
        const uint32_t month_days = productivity_days_in_month(year, month);
        if (ordinal < month_days) break;
        ordinal -= month_days;
        ++month;
    }
    day = static_cast<uint8_t>(ordinal + 1U);
}

uint32_t productivity_utility_minute_stamp(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
    return productivity_utility_date_ordinal(year, month, day) * 1440U + static_cast<uint32_t>(hour) * 60U + minute;
}

void productivity_utility_add_minutes(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute, int32_t delta) {
    int32_t stamp = static_cast<int32_t>(productivity_utility_minute_stamp(year, month, day, hour, minute));
    const int32_t maximum = static_cast<int32_t>(productivity_utility_date_ordinal(2099U, 12U, 31U) * 1440U + 1439U);
    if (delta > 0 && stamp > maximum - delta) stamp = maximum;
    else if (delta < 0 && stamp < -delta) stamp = 0;
    else stamp += delta;
    const uint32_t value = static_cast<uint32_t>(stamp);
    productivity_utility_date_from_ordinal(value / 1440U, year, month, day);
    const uint32_t within = value % 1440U;
    hour = static_cast<uint8_t>(within / 60U);
    minute = static_cast<uint8_t>(within % 60U);
}

int32_t productivity_utility_compare_datetime(uint16_t left_year, uint8_t left_month, uint8_t left_day, uint8_t left_hour, uint8_t left_minute,
                                               uint16_t right_year, uint8_t right_month, uint8_t right_day, uint8_t right_hour, uint8_t right_minute) {
    const uint32_t left = productivity_utility_minute_stamp(left_year, left_month, left_day, left_hour, left_minute);
    const uint32_t right = productivity_utility_minute_stamp(right_year, right_month, right_day, right_hour, right_minute);
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

void productivity_utility_format_datetime(char output[17], uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
    char date[11]{};
    char time[6]{};
    productivity_date_text(date, year, month, day);
    productivity_time_text(time, hour, minute);
    uint32_t length = 0U;
    productivity_append_text(output, 17U, length, date);
    if (length + 1U < 17U) output[length++] = ' ';
    output[length] = 0;
    productivity_append_text(output, 17U, length, time);
}

void productivity_utility_draw_int32(int32_t x, int32_t y, int32_t value, uint32_t color, uint8_t scale = 1U) {
    char text[16]{};
    productivity_utility_format_int32(text, sizeof(text), value);
    draw_text(x, y, text, color, scale);
}
