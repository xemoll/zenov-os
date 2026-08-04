bool leap(std::uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

std::uint8_t dim(std::uint16_t year, std::uint8_t month) {
    constexpr std::array<std::uint8_t, 12> days{31,28,31,30,31,30,31,31,30,31,30,31};
    return month == 2 && leap(year) ? 29 : days.at(month - 1);
}

std::uint32_t ordinal(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    std::uint32_t value = 0;
    for (std::uint16_t y = 2000; y < year; ++y) value += leap(y) ? 366U : 365U;
    for (std::uint8_t m = 1; m < month; ++m) value += dim(year, m);
    return value + day - 1U;
}

enum class Repeat : std::uint8_t { none, daily, weekly, monthly };

struct Reminder {
    bool done{};
    std::uint16_t year{};
    std::uint8_t month{}, day{}, hour{}, minute{};
    std::string title;
    Repeat repeat{Repeat::none};
    std::uint8_t repeat_day{};
};

bool valid_datetime(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    return year >= 2000U && year <= 2099U && month >= 1U && month <= 12U && day >= 1U &&
        day <= dim(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month)) && hour <= 23U && minute <= 59U;
}

bool parse2(std::string_view line, std::size_t pos, unsigned& value) {
    if (pos + 2 > line.size() || line[pos] < '0' || line[pos] > '9' || line[pos + 1] < '0' || line[pos + 1] > '9') return false;
    value = static_cast<unsigned>((line[pos] - '0') * 10 + (line[pos + 1] - '0'));
    return true;
}

bool parse4(std::string_view line, std::size_t pos, unsigned& value) {
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (pos + i >= line.size() || line[pos + i] < '0' || line[pos + i] > '9') return false;
        value = value * 10U + static_cast<unsigned>(line[pos + i] - '0');
    }
    return true;
}

bool repeat_from_code(char code, Repeat& repeat) {
    if (code == 'N') repeat = Repeat::none;
    else if (code == 'D') repeat = Repeat::daily;
    else if (code == 'W') repeat = Repeat::weekly;
    else if (code == 'M') repeat = Repeat::monthly;
    else return false;
    return true;
}

bool parse_reminder(std::string_view line, Reminder& out) {
    if (line.size() < 23 || line.size() > 67 || line[0] != 'R' || line[2] != '|' ||
        (line[3] != '0' && line[3] != '1') || line[4] != '|' || line[9] != '-' || line[12] != '-' ||
        line[15] != '|' || line[18] != ':' || line[21] != '|') return false;
    std::size_t title_offset = 22;
    Repeat repeat = Repeat::none;
    unsigned repeat_day = 0U;
    if (line[1] == '2') {
        if (line.size() < 28 || line[23] != '|' || line[26] != '|' || !repeat_from_code(line[22], repeat) ||
            !parse2(line, 24, repeat_day) || repeat_day < 1U || repeat_day > 31U) return false;
        title_offset = 27;
    } else if (line[1] != '1') return false;

    unsigned year{}, month{}, day{}, hour{}, minute{};
    if (!parse4(line, 5, year) || !parse2(line, 10, month) || !parse2(line, 13, day) ||
        !parse2(line, 16, hour) || !parse2(line, 19, minute) || !valid_datetime(year, month, day, hour, minute)) return false;
    const auto title = line.substr(title_offset);
    if (title.empty() || title.size() > 40) return false;
    for (const char c : title) if (c < 32 || c > 126 || c == '|') return false;
    out = Reminder{line[3] == '1', static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month),
        static_cast<std::uint8_t>(day), static_cast<std::uint8_t>(hour), static_cast<std::uint8_t>(minute), std::string(title), repeat,
        static_cast<std::uint8_t>(repeat_day ? repeat_day : day)};
    return true;
}

std::uint32_t minute_stamp(const Reminder& r) {
    return ordinal(r.year, r.month, r.day) * 1440U + static_cast<std::uint32_t>(r.hour) * 60U + r.minute;
}

Reminder from_stamp(Reminder value, std::uint32_t stamp) {
    std::uint32_t days = stamp / 1440U;
    value.hour = static_cast<std::uint8_t>((stamp % 1440U) / 60U);
    value.minute = static_cast<std::uint8_t>(stamp % 60U);
    value.year = 2000;
    while (value.year < 2099) {
        const std::uint32_t year_days = leap(value.year) ? 366U : 365U;
        if (days < year_days) break;
        days -= year_days;
        ++value.year;
    }
    value.month = 1;
    while (value.month < 12) {
        const std::uint32_t month_days = dim(value.year, value.month);
        if (days < month_days) break;
        days -= month_days;
        ++value.month;
    }
    value.day = static_cast<std::uint8_t>(days + 1U);
    return value;
}

bool add_month(Reminder& reminder) {
    const std::uint32_t month_index = static_cast<std::uint32_t>(reminder.year - 2000U) * 12U + reminder.month - 1U;
    if (month_index >= 1199U) return false;
    const std::uint32_t next = month_index + 1U;
    reminder.year = static_cast<std::uint16_t>(2000U + next / 12U);
    reminder.month = static_cast<std::uint8_t>(next % 12U + 1U);
    const std::uint8_t anchor = reminder.repeat_day ? reminder.repeat_day : reminder.day;
    reminder.day = std::min(anchor, dim(reminder.year, reminder.month));
    return true;
}

bool advance_repeat(Reminder& reminder, const Reminder& now) {
    if (reminder.repeat == Repeat::none) return false;
    if (reminder.repeat == Repeat::daily || reminder.repeat == Repeat::weekly) {
        const std::uint32_t interval = reminder.repeat == Repeat::daily ? 1U : 7U;
        const std::uint32_t maximum = ordinal(2099, 12, 31);
        const std::uint32_t current = ordinal(reminder.year, reminder.month, reminder.day);
        if (current > maximum - interval) return false;
        reminder = from_stamp(reminder, minute_stamp(reminder) + interval * 1440U);
        if (minute_stamp(reminder) <= minute_stamp(now)) {
            const std::uint32_t due_day = ordinal(reminder.year, reminder.month, reminder.day);
            const std::uint32_t now_day = ordinal(now.year, now.month, now.day);
            const std::uint32_t extra = ((now_day - due_day) / interval + 1U) * interval;
            if (extra > maximum || due_day > maximum - extra) return false;
            reminder = from_stamp(reminder, minute_stamp(reminder) + extra * 1440U);
        }
        return true;
    }
    if (!add_month(reminder)) return false;
    for (unsigned guard = 0; guard < 1200; ++guard) {
        if (minute_stamp(reminder) > minute_stamp(now)) return true;
        if (!add_month(reminder)) return false;
    }
    return false;
}

void trim(std::string& text) {
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
}

bool parse_quick(std::string input, const Reminder& now, Repeat default_repeat, Reminder& out) {
    trim(input);
    if (input.empty() || input.size() >= 65) return false;
    out = now;
    out.done = false;
    out.title.clear();
    out.repeat = default_repeat;

    const std::array<std::pair<std::string_view, Repeat>, 4> suffixes{{
        {" !daily", Repeat::daily}, {" !weekly", Repeat::weekly},
        {" !monthly", Repeat::monthly}, {" !once", Repeat::none}
    }};
    for (const auto& suffix : suffixes) {
        if (input.size() >= suffix.first.size() && input.compare(input.size() - suffix.first.size(), suffix.first.size(), suffix.first) == 0) {
            input.resize(input.size() - suffix.first.size());
            trim(input);
            out.repeat = suffix.second;
            break;
        }
    }

    out = from_stamp(out, minute_stamp(now) + 10U);
    const std::size_t marker = input.rfind(" @");
    if (marker != std::string::npos) {
        const std::string schedule = input.substr(marker + 2U);
        input.resize(marker);
        trim(input);
        if (schedule.size() >= 3U && schedule[0] == '+') {
            std::size_t position = 1U;
            std::uint32_t value = 0U;
            while (position < schedule.size() && schedule[position] >= '0' && schedule[position] <= '9') {
                const std::uint32_t digit = static_cast<std::uint32_t>(schedule[position] - '0');
                if (value > (525600U - digit) / 10U) return false;
                value = value * 10U + digit;
                ++position;
            }
            if (position == 1U || position + 1U != schedule.size() || value == 0U) return false;
            std::uint32_t multiplier = schedule[position] == 'm' ? 1U : schedule[position] == 'h' ? 60U : schedule[position] == 'd' ? 1440U : 0U;
            if (!multiplier || value > 525600U / multiplier) return false;
            out = from_stamp(out, minute_stamp(now) + value * multiplier);
        } else {
            if (schedule.size() != 16U || schedule[4] != '-' || schedule[7] != '-' || schedule[10] != ' ' || schedule[13] != ':') return false;
            unsigned year{}, month{}, day{}, hour{}, minute{};
            if (!parse4(schedule, 0, year) || !parse2(schedule, 5, month) || !parse2(schedule, 8, day) ||
                !parse2(schedule, 11, hour) || !parse2(schedule, 14, minute) || !valid_datetime(year, month, day, hour, minute)) return false;
            out.year = static_cast<std::uint16_t>(year);
            out.month = static_cast<std::uint8_t>(month);
            out.day = static_cast<std::uint8_t>(day);
            out.hour = static_cast<std::uint8_t>(hour);
            out.minute = static_cast<std::uint8_t>(minute);
        }
    }
    if (input.empty() || input.size() > 40U || input.find('|') != std::string::npos) return false;
    for (const char c : input) if (c < 32 || c > 126) return false;
    out.repeat_day = out.day;
    out.title = input;
    return true;
}

bool in_seven_day_range(const Reminder& item, const Reminder& now) {
    const auto item_day = ordinal(item.year, item.month, item.day);
    const auto now_day = ordinal(now.year, now.month, now.day);
    return !item.done && item_day >= now_day && item_day <= now_day + 6U;
}
