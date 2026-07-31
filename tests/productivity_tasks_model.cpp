#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
struct Task {
    std::array<char, 65> text{};
    std::uint16_t state_offset{};
    std::uint16_t due_year{};
    std::uint8_t due_month{};
    std::uint8_t due_day{};
    std::uint8_t priority{4};
    bool done{};
    bool waiting{};
};

bool equal_ci(char left, char right) {
    if (left >= 'A' && left <= 'Z') left = static_cast<char>(left + ('a' - 'A'));
    if (right >= 'A' && right <= 'Z') right = static_cast<char>(right + ('a' - 'A'));
    return left == right;
}

bool tag_boundary(char value) {
    return value == ' ' || value == '\t';
}

bool has_token_ci(std::string_view line, std::string_view token) {
    if (token.empty() || token.size() > line.size()) return false;
    for (std::size_t start = 0; start + token.size() <= line.size(); ++start) {
        if (start != 0U && !tag_boundary(line[start - 1U])) continue;
        if (start + token.size() != line.size() && !tag_boundary(line[start + token.size()])) continue;
        bool match = true;
        for (std::size_t index = 0; index < token.size(); ++index) {
            if (!equal_ci(line[start + index], token[index])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

bool leap(std::uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month) {
    constexpr std::array<std::uint8_t, 12> days{31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) return 0U;
    return month == 2U && leap(year) ? 29U : days[month - 1U];
}

bool parse_two(std::string_view text, std::size_t offset, std::uint32_t& value) {
    if (offset + 2U > text.size()) return false;
    if (text[offset] < '0' || text[offset] > '9' || text[offset + 1U] < '0' || text[offset + 1U] > '9') return false;
    value = static_cast<std::uint32_t>(text[offset] - '0') * 10U + static_cast<std::uint32_t>(text[offset + 1U] - '0');
    return true;
}

bool parse_due(std::string_view line, Task& task) {
    constexpr std::size_t token_size = 13;
    for (std::size_t position = 0; position + token_size <= line.size(); ++position) {
        if ((position != 0U && !tag_boundary(line[position - 1U])) ||
            (position + token_size != line.size() && !tag_boundary(line[position + token_size])) ||
            line[position] != '#' || !equal_ci(line[position + 1U], 'D') || line[position + 2U] != '-' ||
            line[position + 7U] != '-' || line[position + 10U] != '-') continue;
        std::uint32_t year = 0U;
        for (std::size_t index = 3U; index < 7U; ++index) {
            if (line[position + index] < '0' || line[position + index] > '9') { year = 0U; break; }
            year = year * 10U + static_cast<std::uint32_t>(line[position + index] - '0');
        }
        std::uint32_t month = 0U, day = 0U;
        if (!year || !parse_two(line, position + 8U, month) || !parse_two(line, position + 11U, day)) continue;
        if (year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U || day > days_in_month(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month))) continue;
        task.due_year = static_cast<std::uint16_t>(year);
        task.due_month = static_cast<std::uint8_t>(month);
        task.due_day = static_cast<std::uint8_t>(day);
        return true;
    }
    return false;
}

bool parse_task(std::string_view line, Task& task) {
    std::size_t marker = 0;
    while (marker < line.size() && (line[marker] == ' ' || line[marker] == '\t')) ++marker;
    if (marker + 5U > line.size() || line[marker] != '-' || line[marker + 1U] != ' ' || line[marker + 2U] != '[' ||
        line[marker + 4U] != ']' || (line[marker + 3U] != ' ' && line[marker + 3U] != 'x' && line[marker + 3U] != 'X')) return false;
    task.state_offset = static_cast<std::uint16_t>(marker + 3U);
    task.done = line[marker + 3U] != ' ';
    const auto payload = line.substr(marker + 5U);
    task.waiting = has_token_ci(payload, "#W");
    task.priority = has_token_ci(payload, "#P1") ? 1U : (has_token_ci(payload, "#P2") ? 2U : (has_token_ci(payload, "#P3") ? 3U : 4U));
    (void)parse_due(payload, task);
    std::size_t input = 0;
    while (input < payload.size() && payload[input] == ' ') ++input;
    std::size_t output = 0;
    while (input < payload.size() && output + 1U < task.text.size()) task.text[output++] = payload[input++];
    task.text[output] = 0;
    return true;
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "productivity_tasks_model: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    Task open{};
    require(parse_task("  - [ ] Ship release #P1 #D-2026-08-03", open), "open task not parsed");
    require(!open.done && open.priority == 1U && open.due_year == 2026U && open.due_month == 8U && open.due_day == 3U, "open metadata mismatch");
    require(open.state_offset == 5U, "state offset mismatch");

    Task done{};
    require(parse_task("- [X] Wait for review #W #P3", done), "done task not parsed");
    require(done.done && done.waiting && done.priority == 3U, "done metadata mismatch");

    Task leap_task{};
    require(parse_task("- [ ] Leap #D-2028-02-29", leap_task), "valid leap date rejected");
    Task invalid_due{};
    require(parse_task("- [ ] Invalid #D-2027-02-29", invalid_due), "task with invalid due rejected entirely");
    require(invalid_due.due_year == 0U, "invalid due date accepted");

    Task partial_tags{};
    require(parse_task("- [ ] Boundaries #P1x #WORK #D-2028-02-29x", partial_tags), "boundary fixture not parsed");
    require(partial_tags.priority == 4U && !partial_tags.waiting && partial_tags.due_year == 0U, "partial metadata token accepted");

    Task malformed{};
    require(!parse_task("- [] malformed", malformed), "malformed checkbox accepted");
    require(!parse_task("plain text", malformed), "plain line accepted");

    std::array<char, 64> mutable_line{};
    constexpr std::string_view source = "- [ ] Toggle";
    std::copy(source.begin(), source.end(), mutable_line.begin());
    Task toggle{};
    require(parse_task(std::string_view(mutable_line.data(), source.size()), toggle), "toggle task not parsed");
    require(mutable_line[toggle.state_offset] == ' ', "initial state mismatch");
    mutable_line[toggle.state_offset] = 'x';
    require(mutable_line[toggle.state_offset] == 'x', "toggle failed");

    std::cout << "PRODUCTIVITY_TASKS_MODEL_OK markdown=checkbox metadata=priority+due+waiting toggle=bounded leap=yes\n";
    return 0;
}
