#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

#include "productivity_utilities_model_calc.inc"
#include "productivity_utilities_model_reminders.inc"

} // namespace

int main() {
    {
        auto [value, error] = eval("2+3*4", false);
        assert(error == CalcError::none && value == 14);
        std::tie(value, error) = eval("(2+3)*4", false);
        assert(error == CalcError::none && value == 20);
        std::tie(value, error) = eval("0xff & 0x0f", true);
        assert(error == CalcError::none && value == 15);
        std::tie(value, error) = eval("1<<31", true);
        assert(error == CalcError::none && static_cast<std::uint32_t>(value) == 0x80000000U);
        std::tie(value, error) = eval("2147483647+1", false);
        assert(error == CalcError::overflow);
        std::tie(value, error) = eval("7/0", false);
        assert(error == CalcError::divide_by_zero);
        std::tie(value, error) = eval("1|2", false);
        assert(error == CalcError::syntax);
    }

    assert(ordinal(2000, 1, 1) == 0);
    assert(ordinal(2000, 3, 1) == 60);
    assert(ordinal(2001, 3, 1) == 425);
    assert(ordinal(2028, 2, 29) + 1 == ordinal(2028, 3, 1));

    Reminder r{};
    assert(parse_reminder("R1|0|2026-08-01|12:30|Legacy reminder", r));
    assert(!r.done && r.repeat == Repeat::none && r.title == "Legacy reminder");
    assert(parse_reminder("R2|0|2026-08-01|12:30|D|01|Review release", r));
    assert(r.repeat == Repeat::daily && r.year == 2026 && r.month == 8 && r.day == 1 && r.hour == 12 && r.minute == 30);
    assert(parse_reminder("R2|1|2026-08-01|12:30|N|01|Completed", r) && r.done);
    assert(!parse_reminder("R2|0|2026-02-30|12:30|D|30|Invalid", r));
    assert(!parse_reminder("R2|0|2026-08-01|25:00|D|01|Invalid", r));
    assert(!parse_reminder("R2|0|2026-08-01|12:30|X|01|Invalid", r));
    assert(!parse_reminder("R2|0|2026-08-01|12:30|D|01|bad|title", r));

    const Reminder now{false, 2026, 8, 1, 12, 29, "now", Repeat::none, 1};
    Reminder quick{};
    assert(parse_quick("Drink water @+10m !daily", now, Repeat::none, quick));
    assert(quick.title == "Drink water" && quick.repeat == Repeat::daily && minute_stamp(quick) - minute_stamp(now) == 10U);
    assert(parse_quick("Review release @2026-08-03 09:30 !weekly", now, Repeat::none, quick));
    assert(quick.year == 2026 && quick.month == 8 && quick.day == 3 && quick.hour == 9 && quick.minute == 30 && quick.repeat == Repeat::weekly);
    assert(parse_quick("Default repeat", now, Repeat::monthly, quick) && quick.repeat == Repeat::monthly);
    assert(!parse_quick("Broken @+0m", now, Repeat::none, quick));
    assert(!parse_quick("Broken @2026-02-30 09:30", now, Repeat::none, quick));

    Reminder daily{false, 2026, 7, 29, 12, 0, "Daily", Repeat::daily, 29};
    assert(advance_repeat(daily, now));
    assert(daily.year == 2026 && daily.month == 8 && daily.day == 2 && daily.hour == 12);

    Reminder weekly{false, 2026, 7, 18, 10, 0, "Weekly", Repeat::weekly, 18};
    assert(advance_repeat(weekly, now));
    assert(weekly.year == 2026 && weekly.month == 8 && weekly.day == 8);

    const Reminder leap_now{false, 2028, 1, 31, 8, 0, "now", Repeat::none, 1};
    Reminder monthly{false, 2028, 1, 31, 9, 0, "Monthly", Repeat::monthly, 31};
    assert(advance_repeat(monthly, leap_now));
    assert(monthly.year == 2028 && monthly.month == 2 && monthly.day == 29 && monthly.repeat_day == 31);
    const Reminder feb_now{false, 2028, 2, 29, 10, 0, "now", Repeat::none, 29};
    assert(advance_repeat(monthly, feb_now));
    assert(monthly.year == 2028 && monthly.month == 3 && monthly.day == 31 && monthly.repeat_day == 31);

    Reminder range_item{false, 2026, 8, 7, 0, 0, "Last included day", Repeat::none, 7};
    assert(in_seven_day_range(range_item, now));
    range_item.day = 8;
    assert(!in_seven_day_range(range_item, now));
    range_item = now;
    range_item.done = true;
    assert(!in_seven_day_range(range_item, now));

    {
        constexpr std::size_t capacity = 48U;
        std::vector<std::uint32_t> earliest;
        const auto consider = [&earliest](std::uint32_t stamp) {
            if (earliest.size() < capacity) {
                earliest.push_back(stamp);
                return;
            }
            const auto latest = std::max_element(earliest.begin(), earliest.end());
            if (stamp < *latest) *latest = stamp;
        };
        for (std::uint32_t stamp = 100U; stamp < 148U; ++stamp) consider(stamp);
        consider(1U);
        std::sort(earliest.begin(), earliest.end());
        assert(earliest.size() == capacity && earliest.front() == 1U && earliest.back() == 146U);
    }

    std::puts("PRODUCTIVITY_UTILITIES_MODEL_OK calc=precedence+programmer+overflow date=gregorian reminders=v2+backward+recurrence+quick-add agenda=minute-order+seven-day+earliest-capacity");
    return 0;
}
