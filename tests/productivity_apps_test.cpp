#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;
#include "../kernel/parts/ui_productivity_model.inc"

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace productivity_model;
    try {
        require(leap_year(2000U) && !leap_year(2100U) && leap_year(2024U), "leap years");
        require(days_in_month(2024U, 2U) == 29U && days_in_month(2023U, 2U) == 28U && days_in_month(2024U, 13U) == 0U, "month lengths");
        Date date{2026U, 7U, 30U};
        require(valid_date(date) && weekday_monday_zero(date) == 3U, "weekday");
        require(shift_day(date, 1) && date.day == 31U, "next day");
        require(shift_day(date, 1) && date.month == 8U && date.day == 1U, "month rollover");
        Date leap{2024U, 2U, 29U};
        require(shift_month(leap, 1) && leap.month == 3U && leap.day == 29U, "leap month");
        Date clamp{2023U, 1U, 31U};
        require(shift_month(clamp, 1) && clamp.month == 2U && clamp.day == 28U, "month clamp");
        require(shift_days(clamp, -28) && clamp.month == 1U && clamp.day == 31U, "day shift");

        Editor editor{};
        static constexpr uint8_t seed[] = "alpha\nbeta";
        require(editor_load(editor, seed, sizeof(seed) - 1U), "editor load");
        editor_home(editor); require(editor.cursor == 6U, "line home");
        editor_move_line(editor, -1); require(editor.cursor == 0U, "move up");
        editor_end(editor); require(editor.cursor == 5U, "line end");
        require(editor_insert(editor, '!'), "insert");
        require(editor_backspace(editor), "backspace");
        editor_move_horizontal(editor, -1);
        require(editor_delete(editor), "delete");

        Editor links{};
        static constexpr uint8_t text[] = "[[One]] #tag\n[[Two]] #two";
        require(editor_load(links, text, sizeof(text) - 1U), "link load");
        require(count_wiki_links(links) == 2U && count_tags(links) == 2U, "link and tag counts");

        std::cout << "PRODUCTIVITY_APPS_MODEL_OK calendar=gregorian editor=bounded links=2 tags=2\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "productivity-apps-test: " << error.what() << '\n';
        return 1;
    }
}
