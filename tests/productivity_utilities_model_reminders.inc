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

struct Reminder {
    bool done{};
    std::uint16_t year{};
    std::uint8_t month{}, day{}, hour{}, minute{};
    std::string title;
};

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

bool parse_reminder(std::string_view line, Reminder& out) {
    if (line.size() < 23 || line.substr(0, 3) != "R1|" || (line[3] != '0' && line[3] != '1') || line[4] != '|' ||
        line[9] != '-' || line[12] != '-' || line[15] != '|' || line[18] != ':' || line[21] != '|') return false;
    unsigned year{}, month{}, day{}, hour{}, minute{};
    if (!parse4(line, 5, year) || !parse2(line, 10, month) || !parse2(line, 13, day) ||
        !parse2(line, 16, hour) || !parse2(line, 19, minute)) return false;
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > dim(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month)) || hour > 23 || minute > 59) return false;
    const auto title = line.substr(22);
    if (title.empty() || title.size() > 40) return false;
    for (const char c : title) if (c < 32 || c > 126 || c == '|') return false;
    out = Reminder{line[3] == '1', static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month), static_cast<std::uint8_t>(day), static_cast<std::uint8_t>(hour), static_cast<std::uint8_t>(minute), std::string(title)};
    return true;
}

std::uint32_t minute_stamp(const Reminder& r) {
    return ordinal(r.year, r.month, r.day) * 1440U + static_cast<std::uint32_t>(r.hour) * 60U + r.minute;
}
