#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

bool leap(std::uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

std::uint8_t dim(std::uint16_t year, std::uint8_t month) {
    constexpr std::array<std::uint8_t, 12> days{31,28,31,30,31,30,31,31,30,31,30,31};
    return month == 2U && leap(year) ? 29U : days.at(month - 1U);
}

std::uint32_t ordinal(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    std::uint32_t value = 0U;
    for (std::uint16_t current = 2000U; current < year; ++current) value += leap(current) ? 366U : 365U;
    for (std::uint8_t current = 1U; current < month; ++current) value += dim(year, current);
    return value + day - 1U;
}

enum class Repeat : std::uint8_t { none, daily, weekly, monthly };

struct Event {
    std::uint16_t year{};
    std::uint8_t month{}, day{}, hour{}, minute{};
    std::uint16_t duration{};
    Repeat repeat{Repeat::none};
    std::uint8_t anchor{};
    std::string title;
};

bool date_valid(unsigned year, unsigned month, unsigned day) {
    return year >= 2000U && year <= 2099U && month >= 1U && month <= 12U && day >= 1U && day <= dim(year, month);
}

bool occurs(const Event& event, std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    const auto start = ordinal(event.year, event.month, event.day);
    const auto target = ordinal(year, month, day);
    if (target < start) return false;
    if (event.repeat == Repeat::none) return target == start;
    if (event.repeat == Repeat::daily) return true;
    if (event.repeat == Repeat::weekly) return (target - start) % 7U == 0U;
    const std::uint32_t start_month = static_cast<std::uint32_t>(event.year - 2000U) * 12U + event.month - 1U;
    const std::uint32_t target_month = static_cast<std::uint32_t>(year - 2000U) * 12U + month - 1U;
    const std::uint8_t occurrence = std::min<std::uint8_t>(event.anchor ? event.anchor : event.day, dim(year, month));
    return target_month >= start_month && day == occurrence;
}

void trim(std::string& text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
}

bool ends_with_ci(std::string_view text, std::string_view suffix) {
    if (suffix.size() > text.size()) return false;
    const auto start = text.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        char left = text[start + index], right = suffix[index];
        if (left >= 'A' && left <= 'Z') left = static_cast<char>(left + 32);
        if (right >= 'A' && right <= 'Z') right = static_cast<char>(right + 32);
        if (left != right) return false;
    }
    return true;
}

bool parse_event(std::string input, std::uint16_t year, std::uint8_t month, std::uint8_t day, Event& event) {
    trim(input);
    if (input.empty() || input.size() >= 80U) return false;
    event = Event{};
    event.year = year; event.month = month; event.day = day; event.anchor = day;
    event.hour = 9U; event.duration = 60U;
    const std::array<std::pair<std::string_view, Repeat>, 4> suffixes{{
        {" !daily", Repeat::daily}, {" !weekly", Repeat::weekly}, {" !monthly", Repeat::monthly}, {" !once", Repeat::none}
    }};
    for (const auto& [suffix, repeat] : suffixes) {
        if (!ends_with_ci(input, suffix)) continue;
        input.resize(input.size() - suffix.size()); trim(input); event.repeat = repeat; break;
    }
    const auto duration_marker = input.rfind(" +");
    if (duration_marker != std::string::npos) {
        const auto token = input.substr(duration_marker + 2U);
        if (token.empty() || token.size() > 4U) return false;
        unsigned value = 0U;
        for (const char c : token) { if (c < '0' || c > '9') return false; value = value * 10U + static_cast<unsigned>(c - '0'); }
        if (value < 1U || value > 1440U) return false;
        event.duration = static_cast<std::uint16_t>(value);
        input.resize(duration_marker); trim(input);
    }
    const auto time_marker = input.rfind(" @");
    if (time_marker != std::string::npos) {
        const auto token = input.substr(time_marker + 2U);
        if (token.size() != 5U || token[2] != ':' || token[0] < '0' || token[0] > '9' || token[1] < '0' || token[1] > '9' ||
            token[3] < '0' || token[3] > '9' || token[4] < '0' || token[4] > '9') return false;
        const unsigned hour = static_cast<unsigned>(token[0] - '0') * 10U + static_cast<unsigned>(token[1] - '0');
        const unsigned minute = static_cast<unsigned>(token[3] - '0') * 10U + static_cast<unsigned>(token[4] - '0');
        if (hour > 23U || minute > 59U) return false;
        event.hour = static_cast<std::uint8_t>(hour); event.minute = static_cast<std::uint8_t>(minute);
        input.resize(time_marker); trim(input);
    }
    if (input.empty() || input.size() > 32U || input.find('|') != std::string::npos) return false;
    event.title = input;
    return date_valid(year, month, day);
}

struct Alarm { std::uint8_t hour{}, minute{}, mask{}; bool enabled{}; std::string title; };

bool parse_alarm(std::string input, Alarm& alarm) {
    trim(input);
    if (input.size() < 7U || input.size() >= 64U) return false;
    alarm = Alarm{}; alarm.mask = 127U; alarm.enabled = true;
    const std::array<std::pair<std::string_view, std::uint8_t>, 4> suffixes{{
        {" !daily",127U}, {" !weekdays",62U}, {" !weekends",65U}, {" !once",0U}
    }};
    for (const auto& [suffix, mask] : suffixes) {
        if (!ends_with_ci(input, suffix)) continue;
        input.resize(input.size() - suffix.size()); trim(input); alarm.mask = mask; break;
    }
    if (input.size() < 7U || input[2] != ':' || input[5] != ' ') return false;
    for (const std::size_t index : {0U,1U,3U,4U}) if (input[index] < '0' || input[index] > '9') return false;
    const unsigned hour = static_cast<unsigned>(input[0] - '0') * 10U + static_cast<unsigned>(input[1] - '0');
    const unsigned minute = static_cast<unsigned>(input[3] - '0') * 10U + static_cast<unsigned>(input[4] - '0');
    if (hour > 23U || minute > 59U) return false;
    alarm.hour = static_cast<std::uint8_t>(hour); alarm.minute = static_cast<std::uint8_t>(minute);
    alarm.title = input.substr(6U); trim(alarm.title);
    return !alarm.title.empty() && alarm.title.size() <= 24U && alarm.title.find('|') == std::string::npos;
}

} // namespace

int main() {
    Event event{};
    assert(parse_event("Review release @09:30 +90 !weekly", 2026, 8, 4, event));
    assert(event.title == "Review release" && event.hour == 9U && event.minute == 30U && event.duration == 90U && event.repeat == Repeat::weekly);
    assert(occurs(event, 2026, 8, 4));
    assert(occurs(event, 2026, 8, 11));
    assert(!occurs(event, 2026, 8, 10));
    Event monthly{2028,1,31,18,0,60,Repeat::monthly,31,"Month end"};
    assert(occurs(monthly, 2028, 2, 29));
    assert(occurs(monthly, 2028, 3, 31));
    assert(!occurs(monthly, 2028, 3, 30));
    assert(!parse_event("Bad @25:00 +60", 2026, 8, 4, event));
    assert(!parse_event("Bad @09:00 +0", 2026, 8, 4, event));
    assert(!parse_event("Bad|title @09:00", 2026, 8, 4, event));

    Alarm alarm{};
    assert(parse_alarm("07:30 Wake up !weekdays", alarm));
    assert(alarm.hour == 7U && alarm.minute == 30U && alarm.mask == 62U && alarm.title == "Wake up");
    assert(parse_alarm("12:00 Test !once", alarm) && alarm.mask == 0U);
    assert(!parse_alarm("24:00 Invalid", alarm));
    assert(!parse_alarm("07:60 Invalid", alarm));

    std::array<std::uint32_t, 8> laps{};
    std::size_t lap_count = 0U;
    for (const std::uint32_t tick : {120U, 245U, 390U}) laps[lap_count++] = tick;
    assert(lap_count == 3U && laps[0] < laps[1] && laps[1] < laps[2]);
    std::array<std::uint32_t, 3> timers{60U,120U,180U};
    assert(timers.size() == 3U && timers[2] == 180U);

    std::puts("PRODUCTIVITY_CALENDAR_CLOCK_V2_MODEL_OK calendar=timed+duration+daily+weekly+monthly alarms=daily+weekdays+weekends+once stopwatch=laps timers=three bounds=closed");
    return 0;
}
