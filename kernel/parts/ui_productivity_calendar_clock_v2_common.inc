bool productivity_calendar_clock_v2_append_text(char* output, uint32_t capacity, uint32_t& length, const char* text) {
    if (!output || !text || !capacity || length >= capacity) return false;
    for (uint32_t index = 0U; text[index]; ++index) {
        if (length + 1U >= capacity) return false;
        output[length++] = text[index];
    }
    output[length] = 0;
    return true;
}

void productivity_calendar_clock_v2_append_uint(char* output, uint32_t capacity, uint32_t& length,
                                                uint32_t value, uint8_t minimum_digits = 0U) {
    if (!output || !capacity || length >= capacity) return;
    char reversed[11]{};
    constexpr uint8_t reversed_capacity = static_cast<uint8_t>(sizeof(reversed));
    uint8_t count = 0U;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value && count < reversed_capacity);
    while (count < minimum_digits && count < reversed_capacity) reversed[count++] = '0';
    while (count) {
        if (length + 1U >= capacity) return;
        output[length++] = reversed[--count];
    }
    output[length] = 0;
}

bool productivity_calendar_clock_v2_parse_digits(const char* text, uint32_t offset, uint8_t count, uint32_t& value) {
    value = 0U;
    if (!text) return false;
    for (uint8_t index = 0U; index < count; ++index) {
        const char digit = text[offset + index];
        if (digit < '0' || digit > '9') return false;
        value = value * 10U + static_cast<uint32_t>(digit - '0');
    }
    return true;
}

bool productivity_calendar_clock_v2_title_valid(const char* title, uint8_t size, uint8_t capacity) {
    if (!title || !size || size >= capacity) return false;
    for (uint8_t index = 0U; index < size; ++index) {
        const char value = title[index];
        if (value < 32 || value > 126 || value == '|') return false;
    }
    return true;
}

void productivity_calendar_clock_v2_trim(char* text, uint8_t& size) {
    if (!text) return;
    uint8_t start = 0U;
    while (start < size && (text[start] == ' ' || text[start] == '\t')) ++start;
    if (start) {
        for (uint8_t index = start; index < size; ++index) text[index - start] = text[index];
        size = static_cast<uint8_t>(size - start);
    }
    while (size && (text[size - 1U] == ' ' || text[size - 1U] == '\t')) --size;
    text[size] = 0;
}

bool productivity_calendar_clock_v2_suffix(const char* text, uint8_t size, const char* suffix) {
    const uint32_t suffix_size = string_length(suffix);
    if (!text || !suffix || suffix_size > size) return false;
    const uint32_t start = static_cast<uint32_t>(size) - suffix_size;
    for (uint32_t index = 0U; index < suffix_size; ++index)
        if (!productivity_ascii_equal_ci(text[start + index], suffix[index])) return false;
    return true;
}

char productivity_calendar_v2_repeat_code(ProductivityCalendarRepeat repeat) {
    if (repeat == ProductivityCalendarRepeat::daily) return 'D';
    if (repeat == ProductivityCalendarRepeat::weekly) return 'W';
    if (repeat == ProductivityCalendarRepeat::monthly) return 'M';
    return 'N';
}

bool productivity_calendar_v2_repeat_from_code(char code, ProductivityCalendarRepeat& repeat) {
    if (code == 'N') repeat = ProductivityCalendarRepeat::none;
    else if (code == 'D') repeat = ProductivityCalendarRepeat::daily;
    else if (code == 'W') repeat = ProductivityCalendarRepeat::weekly;
    else if (code == 'M') repeat = ProductivityCalendarRepeat::monthly;
    else return false;
    return true;
}

const char* productivity_calendar_v2_repeat_text(ProductivityCalendarRepeat repeat) {
    if (repeat == ProductivityCalendarRepeat::daily) return "DAILY";
    if (repeat == ProductivityCalendarRepeat::weekly) return "WEEKLY";
    if (repeat == ProductivityCalendarRepeat::monthly) return "MONTHLY";
    return "ONCE";
}

bool productivity_calendar_v2_date_valid(uint32_t year, uint32_t month, uint32_t day) {
    return year >= 2000U && year <= 2099U && month >= 1U && month <= 12U && day >= 1U &&
        day <= productivity_days_in_month(static_cast<uint16_t>(year), static_cast<uint8_t>(month));
}

uint32_t productivity_calendar_v2_ordinal(uint16_t year, uint8_t month, uint8_t day) {
    uint32_t ordinal = 0U;
    for (uint16_t current = 2000U; current < year; ++current) ordinal += productivity_is_leap(current) ? 366U : 365U;
    for (uint8_t current = 1U; current < month; ++current) ordinal += productivity_days_in_month(year, current);
    return ordinal + static_cast<uint32_t>(day - 1U);
}

bool productivity_calendar_v2_from_ordinal(uint32_t ordinal, uint16_t& year, uint8_t& month, uint8_t& day) {
    const uint32_t maximum = productivity_calendar_v2_ordinal(2099U, 12U, 31U);
    if (ordinal > maximum) return false;
    year = 2000U;
    while (year < 2099U) {
        const uint32_t count = productivity_is_leap(year) ? 366U : 365U;
        if (ordinal < count) break;
        ordinal -= count;
        ++year;
    }
    month = 1U;
    while (month < 12U) {
        const uint32_t count = productivity_days_in_month(year, month);
        if (ordinal < count) break;
        ordinal -= count;
        ++month;
    }
    day = static_cast<uint8_t>(ordinal + 1U);
    return true;
}

uint32_t productivity_calendar_v2_month_index(uint16_t year, uint8_t month) {
    return static_cast<uint32_t>(year - 2000U) * 12U + static_cast<uint32_t>(month - 1U);
}

bool productivity_calendar_v2_occurs_on(const ProductivityCalendarEventV2& event, uint16_t year, uint8_t month, uint8_t day) {
    const uint32_t event_ordinal = productivity_calendar_v2_ordinal(event.year, event.month, event.day);
    const uint32_t target_ordinal = productivity_calendar_v2_ordinal(year, month, day);
    if (target_ordinal < event_ordinal) return false;
    if (event.repeat == ProductivityCalendarRepeat::none) return target_ordinal == event_ordinal;
    if (event.repeat == ProductivityCalendarRepeat::daily) return true;
    if (event.repeat == ProductivityCalendarRepeat::weekly) return (target_ordinal - event_ordinal) % 7U == 0U;
    if (productivity_calendar_v2_month_index(year, month) < productivity_calendar_v2_month_index(event.year, event.month)) return false;
    const uint8_t anchor = event.repeat_day ? event.repeat_day : event.day;
    const uint8_t occurrence_day = anchor > productivity_days_in_month(year, month) ? productivity_days_in_month(year, month) : anchor;
    return day == occurrence_day;
}

bool productivity_calendar_v2_next_occurrence(const ProductivityCalendarEventV2& event, uint16_t after_year, uint8_t after_month, uint8_t after_day,
                                              uint16_t& year, uint8_t& month, uint8_t& day) {
    const uint32_t start = productivity_calendar_v2_ordinal(event.year, event.month, event.day);
    const uint32_t after = productivity_calendar_v2_ordinal(after_year, after_month, after_day);
    if (event.repeat == ProductivityCalendarRepeat::none) {
        if (start < after) return false;
        year = event.year; month = event.month; day = event.day;
        return true;
    }
    if (event.repeat == ProductivityCalendarRepeat::daily || event.repeat == ProductivityCalendarRepeat::weekly) {
        const uint32_t interval = event.repeat == ProductivityCalendarRepeat::daily ? 1U : 7U;
        uint32_t target = start;
        if (target < after) target += ((after - target + interval - 1U) / interval) * interval;
        return productivity_calendar_v2_from_ordinal(target, year, month, day);
    }
    uint32_t month_index = productivity_calendar_v2_month_index(after_year, after_month);
    const uint32_t start_month = productivity_calendar_v2_month_index(event.year, event.month);
    if (month_index < start_month) month_index = start_month;
    for (; month_index < 1200U; ++month_index) {
        year = static_cast<uint16_t>(2000U + month_index / 12U);
        month = static_cast<uint8_t>(month_index % 12U + 1U);
        const uint8_t anchor = event.repeat_day ? event.repeat_day : event.day;
        day = anchor > productivity_days_in_month(year, month) ? productivity_days_in_month(year, month) : anchor;
        if (productivity_calendar_v2_ordinal(year, month, day) >= after) return true;
    }
    return false;
}

void productivity_calendar_v2_selected_date_text(char output[11]) {
    productivity_date_text(output, productivity_calendar_year, productivity_calendar_month, productivity_calendar_day);
}
