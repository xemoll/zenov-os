#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class Filter : std::uint8_t { today, overdue, planned, open, done, all };

struct Task {
    std::array<char, 65> text{};
    std::uint16_t line_start{};
    std::uint16_t line_end{};
    std::uint16_t payload_offset{};
    std::uint16_t state_offset{};
    std::uint16_t due_year{};
    std::uint8_t due_month{};
    std::uint8_t due_day{};
    std::uint8_t priority{4};
    std::uint32_t line_hash{};
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

std::uint32_t line_hash(std::string_view data, std::size_t start, std::size_t end) {
    if (end < start || end > data.size()) return 0U;
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = start; index < end; ++index) {
        hash ^= static_cast<std::uint8_t>(data[index]);
        hash = static_cast<std::uint32_t>(static_cast<std::uint64_t>(hash) * 16777619ULL);
    }
    return hash;
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
    constexpr std::size_t token_size = 13U;
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
        if (year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U ||
            day > days_in_month(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month))) continue;
        task.due_year = static_cast<std::uint16_t>(year);
        task.due_month = static_cast<std::uint8_t>(month);
        task.due_day = static_cast<std::uint8_t>(day);
        return true;
    }
    return false;
}

bool parse_task(std::string_view document, std::size_t line_start, std::size_t line_end, Task& task) {
    if (line_start >= line_end || line_end > document.size() || line_end > 0xFFFFU) return false;
    std::size_t marker = line_start;
    while (marker < line_end && (document[marker] == ' ' || document[marker] == '\t')) ++marker;
    if (marker + 5U >= line_end || document[marker] != '-' || document[marker + 1U] != ' ' || document[marker + 2U] != '[' ||
        document[marker + 4U] != ']' || (document[marker + 3U] != ' ' && document[marker + 3U] != 'x' && document[marker + 3U] != 'X') ||
        !tag_boundary(document[marker + 5U])) return false;
    std::size_t payload_start = marker + 5U;
    while (payload_start < line_end && tag_boundary(document[payload_start])) ++payload_start;
    if (payload_start >= line_end) return false;
    task = Task{};
    task.line_start = static_cast<std::uint16_t>(line_start);
    task.line_end = static_cast<std::uint16_t>(line_end);
    task.payload_offset = static_cast<std::uint16_t>(payload_start);
    task.state_offset = static_cast<std::uint16_t>(marker + 3U);
    task.line_hash = line_hash(document, line_start, line_end);
    task.done = document[marker + 3U] != ' ';
    const auto payload = document.substr(payload_start, line_end - payload_start);
    task.waiting = has_token_ci(payload, "#W");
    task.priority = has_token_ci(payload, "#P1") ? 1U : (has_token_ci(payload, "#P2") ? 2U : (has_token_ci(payload, "#P3") ? 3U : 4U));
    (void)parse_due(payload, task);
    std::size_t input = 0U;
    while (input < payload.size() && payload[input] == ' ') ++input;
    std::size_t output = 0U;
    while (input < payload.size() && output + 1U < task.text.size()) task.text[output++] = payload[input++];
    task.text[output] = 0;
    return true;
}

std::vector<Task> scan(std::string_view document) {
    std::vector<Task> tasks;
    std::size_t position = 0U;
    while (position < document.size()) {
        const std::size_t start = position;
        while (position < document.size() && document[position] != '\n') ++position;
        const std::size_t end = position;
        if (position < document.size()) ++position;
        Task task{};
        if (parse_task(document, start, end, task)) tasks.push_back(task);
    }
    return tasks;
}

bool validate(const std::string& document, const Task& task) {
    if (task.line_start >= task.line_end || task.line_end > document.size() || task.payload_offset >= task.line_end ||
        task.state_offset < task.line_start + 3U || task.state_offset + 2U >= task.line_end ||
        task.payload_offset <= task.state_offset + 2U) return false;
    const std::size_t state = task.state_offset;
    return document[state - 3U] == '-' && document[state - 2U] == ' ' && document[state - 1U] == '[' && document[state + 1U] == ']' &&
        (document[state] == ' ' || document[state] == 'x' || document[state] == 'X') && tag_boundary(document[state + 2U]) &&
        line_hash(document, task.line_start, task.line_end) == task.line_hash;
}

bool edit_payload(std::string& document, const Task& task, std::string_view payload) {
    if (payload.empty() || payload.size() >= 65U || !validate(document, task)) return false;
    document.replace(task.payload_offset, task.line_end - task.payload_offset, payload);
    return true;
}

bool toggle(std::string& document, const Task& task) {
    if (!validate(document, task)) return false;
    document[task.state_offset] = document[task.state_offset] == ' ' ? 'x' : ' ';
    return true;
}

bool erase_line(std::string& document, const Task& task) {
    if (!validate(document, task)) return false;
    std::size_t end = task.line_end;
    if (end < document.size() && document[end] == '\n') ++end;
    document.erase(task.line_start, end - task.line_start);
    return true;
}

int compare_date(const Task& task, std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    if (!task.due_year) return 2;
    if (task.due_year != year) return task.due_year < year ? -1 : 1;
    if (task.due_month != month) return task.due_month < month ? -1 : 1;
    if (task.due_day != day) return task.due_day < day ? -1 : 1;
    return 0;
}

bool matches(const Task& task, Filter filter, std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    const int relation = compare_date(task, year, month, day);
    if (filter == Filter::today) return !task.done && relation == 0;
    if (filter == Filter::overdue) return !task.done && relation < 0;
    if (filter == Filter::planned) return !task.done && task.due_year && relation >= 0 && relation <= 1;
    if (filter == Filter::open) return !task.done;
    if (filter == Filter::done) return task.done;
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
    const std::string line = "  - [ ] Ship release #P1 #D-2026-08-03";
    require(parse_task(line, 0U, line.size(), open), "open task not parsed");
    require(!open.done && open.priority == 1U && open.due_year == 2026U && open.due_month == 8U && open.due_day == 3U, "open metadata mismatch");
    require(open.state_offset == 5U && open.payload_offset == 8U, "mutation offsets mismatch");

    Task done{};
    const std::string done_line = "- [X] Wait for review #W #P3";
    require(parse_task(done_line, 0U, done_line.size(), done), "done task not parsed");
    require(done.done && done.waiting && done.priority == 3U, "done metadata mismatch");

    Task leap_task{};
    const std::string leap_line = "- [ ] Leap #D-2028-02-29";
    require(parse_task(leap_line, 0U, leap_line.size(), leap_task), "valid leap date rejected");
    Task invalid_due{};
    const std::string invalid_line = "- [ ] Invalid #D-2027-02-29";
    require(parse_task(invalid_line, 0U, invalid_line.size(), invalid_due), "task with invalid due rejected entirely");
    require(invalid_due.due_year == 0U, "invalid due date accepted");

    Task partial_tags{};
    const std::string boundary_line = "- [ ] Boundaries #P1x #WORK #D-2028-02-29x";
    require(parse_task(boundary_line, 0U, boundary_line.size(), partial_tags), "boundary fixture not parsed");
    require(partial_tags.priority == 4U && !partial_tags.waiting && partial_tags.due_year == 0U, "partial metadata token accepted");

    Task malformed{};
    const std::string malformed_line = "- [] malformed";
    require(!parse_task(malformed_line, 0U, malformed_line.size(), malformed), "malformed checkbox accepted");
    const std::string missing_separator = "- [ ]No separator";
    require(!parse_task(missing_separator, 0U, missing_separator.size(), malformed), "missing checkbox separator accepted");

    std::string document = "# Tasks\n\n- [ ] Ship #P1 #D-2099-12-31\n- [ ] Temporary\n";
    auto tasks = scan(document);
    require(tasks.size() == 2U, "document scan mismatch");
    const Task stale = tasks[0];
    require(edit_payload(document, tasks[0], "Ship updated #P1 #D-2099-12-31"), "guarded edit failed");
    require(!toggle(document, stale), "stale task offset accepted after edit");
    tasks = scan(document);
    require(tasks.size() == 2U && std::string(tasks[0].text.data()) == "Ship updated #P1 #D-2099-12-31", "edited payload not rescanned");
    require(document.find("- [ ] Ship updated #P1 #D-2099-12-31\n") != std::string::npos, "edit removed canonical checkbox separator");
    require(toggle(document, tasks[0]), "guarded toggle failed");
    tasks = scan(document);
    require(tasks[0].done, "toggle did not persist in model");
    require(erase_line(document, tasks[1]), "guarded delete failed");
    require(scan(document).size() == 1U && document.find("Temporary") == std::string::npos, "line delete mismatch");

    Task today{};
    const std::string today_line = "- [ ] Today #D-2026-08-04";
    require(parse_task(today_line, 0U, today_line.size(), today), "today task not parsed");
    Task overdue{};
    const std::string overdue_line = "- [ ] Late #D-2026-08-03";
    require(parse_task(overdue_line, 0U, overdue_line.size(), overdue), "overdue task not parsed");
    Task planned{};
    const std::string planned_line = "- [ ] Future #D-2099-12-31";
    require(parse_task(planned_line, 0U, planned_line.size(), planned), "planned task not parsed");
    require(matches(today, Filter::today, 2026, 8, 4), "Today smart view mismatch");
    require(matches(overdue, Filter::overdue, 2026, 8, 4), "Overdue smart view mismatch");
    require(matches(planned, Filter::planned, 2026, 8, 4), "Planned smart view mismatch");
    require(!matches(overdue, Filter::planned, 2026, 8, 4), "overdue leaked into Planned");

    std::cout << "PRODUCTIVITY_TASKS_MODEL_OK markdown=checkbox metadata=priority+due+waiting views=today+overdue+planned crud=guarded-edit+toggle+delete stale=fail-closed leap=yes\n";
    return 0;
}
