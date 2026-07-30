namespace productivity {
using productivity_model::Date;
using productivity_model::Editor;

enum class Page : uint8_t { clock, calendar, notes, notepad };
enum class DocumentState : uint8_t { unloaded, ready, missing, blocked, save_failed, saved };

constexpr uint8_t page_count = 4U;
constexpr int32_t sidebar_x_offset = 14;
constexpr int32_t sidebar_width = 104;
constexpr int32_t content_x_offset = 132;
constexpr int32_t content_width = 364;
constexpr int32_t page_button_height = 42;
constexpr int32_t editor_x_offset = 144;
constexpr int32_t editor_y_offset = 104;
constexpr int32_t editor_width = 340;
constexpr int32_t editor_height = 232;
constexpr uint32_t editor_columns = 52U;
constexpr uint32_t editor_rows = 18U;
constexpr int32_t editor_line_height = 12;

bool opened = false;
Page page = Page::clock;
View return_view = View::terminal;
Date today{2000U, 1U, 1U};
Date selected_date{2000U, 1U, 1U};
Editor daily_editor{};
Editor scratch_editor{};
DocumentState daily_state = DocumentState::unloaded;
DocumentState scratch_state = DocumentState::unloaded;
Date daily_loaded_date{0U, 0U, 0U};
uint8_t io_buffer[productivity_model::editor_capacity]{};
uint32_t editor_scroll_line = 0U;

struct RtcSnapshot {
    bool valid;
    uint8_t hour, minute, second;
    Date date;
};

void serial_u32(uint32_t value) {
    char reverse[10]; uint32_t count = 0U;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    while (count) serial::put(reverse[--count]);
}

bool query_contains(const char* text) {
    if (!text || launcher_query_length == 0U) return false;
    const uint32_t length = static_cast<uint32_t>(string_length(text));
    if (launcher_query_length > length) return false;
    for (uint32_t start = 0U; start + launcher_query_length <= length; ++start) {
        bool match = true;
        for (uint32_t at = 0U; at < launcher_query_length; ++at) {
            if (launcher_lower(text[start + at]) != launcher_lower(launcher_query[at])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

bool workspace_query_matches() {
    return query_contains("workspace") || query_contains("clock") || query_contains("time") ||
        query_contains("calendar") || query_contains("notes") || query_contains("notepad");
}

bool same_date(const Date& left, const Date& right) {
    return left.year == right.year && left.month == right.month && left.day == right.day;
}

bool rtc_snapshot(RtcSnapshot& output) {
    output = RtcSnapshot{false, 0U, 0U, 0U, Date{2000U,1U,1U}};
    for (uint32_t retry = 0U; retry < 8U; ++retry) {
        if (cmos_read(0x0AU) & 0x80U) continue;
        uint8_t second = cmos_read(0x00U);
        uint8_t minute = cmos_read(0x02U);
        uint8_t hour = cmos_read(0x04U);
        uint8_t day = cmos_read(0x07U);
        uint8_t month = cmos_read(0x08U);
        uint8_t year = cmos_read(0x09U);
        uint8_t century = cmos_read(0x32U);
        const uint8_t status_b = cmos_read(0x0BU);
        if (cmos_read(0x0AU) & 0x80U) continue;
        if (second != cmos_read(0x00U) || minute != cmos_read(0x02U) ||
            day != cmos_read(0x07U) || month != cmos_read(0x08U) || year != cmos_read(0x09U)) continue;
        const bool pm = (hour & 0x80U) != 0U;
        hour = static_cast<uint8_t>(hour & 0x7FU);
        if ((status_b & 0x04U) == 0U) {
            second = bcd_to_binary(second); minute = bcd_to_binary(minute); hour = bcd_to_binary(hour);
            day = bcd_to_binary(day); month = bcd_to_binary(month); year = bcd_to_binary(year);
            century = bcd_to_binary(century);
        }
        if ((status_b & 0x02U) == 0U) {
            if (pm && hour < 12U) hour = static_cast<uint8_t>(hour + 12U);
            else if (!pm && hour == 12U) hour = 0U;
        }
        const uint32_t full_year = century >= 19U && century <= 99U
            ? static_cast<uint32_t>(century) * 100U + year
            : 2000U + year;
        const Date date{full_year, month, day};
        if (hour >= 24U || minute >= 60U || second >= 60U || !productivity_model::valid_date(date)) continue;
        output = RtcSnapshot{true, hour, minute, second, date};
        return true;
    }
    return false;
}

void append_digit(char* output, uint32_t& length, uint8_t value) {
    output[length++] = static_cast<char>('0' + value);
}

void append_two_digits(char* output, uint32_t& length, uint8_t value) {
    append_digit(output, length, static_cast<uint8_t>(value / 10U));
    append_digit(output, length, static_cast<uint8_t>(value % 10U));
}

void append_year(char* output, uint32_t& length, uint32_t year) {
    output[length++] = static_cast<char>('0' + (year / 1000U) % 10U);
    output[length++] = static_cast<char>('0' + (year / 100U) % 10U);
    output[length++] = static_cast<char>('0' + (year / 10U) % 10U);
    output[length++] = static_cast<char>('0' + year % 10U);
}

void format_date(const Date& date, char output[11]) {
    uint32_t length = 0U;
    append_year(output, length, date.year); output[length++] = '-';
    append_two_digits(output, length, date.month); output[length++] = '-';
    append_two_digits(output, length, date.day); output[length] = 0;
}

void build_daily_path(const Date& date, char output[48]) {
    uint32_t length = 0U;
    static constexpr char prefix[] = "/docs/note-";
    for (uint32_t i = 0U; prefix[i]; ++i) output[length++] = prefix[i];
    append_year(output, length, date.year); output[length++] = '-';
    append_two_digits(output, length, date.month); output[length++] = '-';
    append_two_digits(output, length, date.day);
    static constexpr char suffix[] = ".md";
    for (uint32_t i = 0U; suffix[i]; ++i) output[length++] = suffix[i];
    output[length] = 0;
}

const char* page_name(Page value) {
    static constexpr const char* names[page_count] = {"Clock", "Calendar", "Notes", "Notepad"};
    const uint8_t index = static_cast<uint8_t>(value);
    return index < page_count ? names[index] : "Workspace";
}

const char* document_status(DocumentState state, bool dirty) {
    if (dirty) return "Unsaved";
    if (state == DocumentState::saved) return "Saved";
    if (state == DocumentState::blocked) return "Blocked";
    if (state == DocumentState::save_failed) return "Save failed";
    if (state == DocumentState::missing) return "New document";
    return "Local file";
}

bool load_editor(Editor& editor, const char* path, const char* initial, DocumentState& state) {
    uint32_t size = 0U;
    const FsResult result = storage::guarded_read_file_result(path, io_buffer, sizeof(io_buffer) - 1U, size);
    if (result.ok()) {
        if (!productivity_model::editor_load(editor, io_buffer, size)) { state = DocumentState::blocked; return false; }
        state = DocumentState::ready;
        serial::write("UI_PRODUCTIVITY_LOAD_OK path="); serial::write(path);
        serial::write(" bytes="); serial_u32(size); serial::write("\r\n");
        return true;
    }
    if (result.status != FsStatus::not_found) { productivity_model::editor_clear(editor); state = DocumentState::blocked; return false; }
    if (!productivity_model::editor_load(editor, reinterpret_cast<const uint8_t*>(initial), static_cast<uint32_t>(string_length(initial)))) {
        state = DocumentState::blocked; return false;
    }
    editor.dirty = true;
    state = DocumentState::missing;
    serial::write("UI_PRODUCTIVITY_NEW path="); serial::line(path);
    return true;
}

bool save_editor(Editor& editor, const char* path, DocumentState& state) {
    const FsResult result = storage::guarded_write_file_result(path, reinterpret_cast<const uint8_t*>(editor.data), editor.length, false);
    if (!result.durable() || !storage::sync_metadata()) { state = DocumentState::save_failed; return false; }
    editor.dirty = false; state = DocumentState::saved;
    serial::write("UI_PRODUCTIVITY_SAVE_OK path="); serial::line(path);
    return true;
}

void ensure_today() {
    RtcSnapshot snapshot{};
    if (rtc_snapshot(snapshot)) today = snapshot.date;
    if (!productivity_model::valid_date(selected_date)) selected_date = today;
}

void ensure_daily_note() {
    ensure_today();
    if (same_date(daily_loaded_date, selected_date) && daily_state != DocumentState::unloaded) return;
    char path[48]; build_daily_path(selected_date, path);
    char heading[48] = "# Daily note ";
    uint32_t length = static_cast<uint32_t>(string_length(heading));
    char date[11]; format_date(selected_date, date);
    for (uint32_t i = 0U; date[i] && length + 1U < sizeof(heading); ++i) heading[length++] = date[i];
    if (length + 2U < sizeof(heading)) { heading[length++] = '\n'; heading[length++] = '\n'; }
    heading[length] = 0;
    (void)load_editor(daily_editor, path, heading, daily_state);
    daily_loaded_date = selected_date;
    editor_scroll_line = 0U;
}

void ensure_scratchpad() {
    if (scratch_state != DocumentState::unloaded) return;
    (void)load_editor(scratch_editor, "/docs/scratchpad.txt", "Scratchpad\n\n", scratch_state);
    editor_scroll_line = 0U;
}

void select_page(Page next) {
    page = next;
    if (page == Page::calendar) ensure_today();
    else if (page == Page::notes) ensure_daily_note();
    else if (page == Page::notepad) ensure_scratchpad();
    serial::write("UI_PRODUCTIVITY_PAGE "); serial::line(page_name(page));
}

void open_from_launcher() {
    if (active_view == View::launcher) {
        return_view = launcher_return_view;
        active_view = launcher_return_view;
        launcher_query_length = 0U; launcher_query[0] = 0;
    } else return_view = active_view;
    opened = true;
    ensure_today(); selected_date = today;
    select_page(Page::clock);
    scene_transition_pending = true;
    refresh_desktop();
    serial::line("UI_PRODUCTIVITY_OPEN_OK");
}

void close() {
    opened = false;
    active_view = return_view;
    scene_transition_pending = true;
    refresh_desktop();
    serial::line("UI_PRODUCTIVITY_CLOSE_OK");
}

