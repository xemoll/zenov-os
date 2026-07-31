#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
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
    assert(parse_reminder("R1|0|2026-08-01|12:30|Review release", r));
    assert(!r.done && r.year == 2026 && r.month == 8 && r.day == 1 && r.hour == 12 && r.minute == 30);
    assert(!parse_reminder("R1|0|2026-02-30|12:30|Invalid", r));
    assert(!parse_reminder("R1|0|2026-08-01|25:00|Invalid", r));
    assert(!parse_reminder("R1|0|2026-08-01|12:30|bad|title", r));

    Reminder now{false, 2026, 8, 1, 12, 29, "now"};
    Reminder due{false, 2026, 8, 1, 12, 30, "due"};
    assert(minute_stamp(due) - minute_stamp(now) == 1U);

    std::puts("PRODUCTIVITY_UTILITIES_MODEL_OK calc=precedence+programmer+overflow date=gregorian reminders=canonical+bounded agenda=minute-order");
    return 0;
}
