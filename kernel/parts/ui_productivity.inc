enum class ProductivityApp : uint8_t { notes, calendar, clock };

constexpr uint32_t productivity_note_capacity = 12U;
constexpr uint32_t productivity_note_buffer_capacity = 2048U;
constexpr uint32_t productivity_note_visible_rows = 8U;
constexpr uint32_t productivity_event_capacity = 16U;

ProductivityApp productivity_app = ProductivityApp::notes;
bool productivity_overlay_open = false;
bool productivity_storage_ready = false;

storage::BrowserEntry productivity_notes[productivity_note_capacity]{};
uint8_t productivity_note_visible[productivity_note_capacity]{};
uint32_t productivity_note_count = 0U;
uint32_t productivity_note_visible_count = 0U;
uint32_t productivity_note_focus = 0U;
uint32_t productivity_note_scroll = 0U;
char productivity_note_path[48]{};
char productivity_note_text[productivity_note_buffer_capacity]{};
uint32_t productivity_note_size = 0U;
bool productivity_note_editing = false;
bool productivity_note_dirty = false;
bool productivity_note_searching = false;
char productivity_note_query[24]{};
uint8_t productivity_note_query_size = 0U;
char productivity_backlinks[3][48]{};
uint8_t productivity_backlink_count = 0U;

struct ProductivityEvent {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    char title[25];
};

ProductivityEvent productivity_events[productivity_event_capacity]{};
uint8_t productivity_event_count = 0U;
uint16_t productivity_calendar_year = 2026U;
uint8_t productivity_calendar_month = 1U;
uint8_t productivity_calendar_day = 1U;
bool productivity_event_editing = false;
char productivity_event_title[25]{};
uint8_t productivity_event_title_size = 0U;

bool productivity_stopwatch_running = false;
uint32_t productivity_stopwatch_started = 0U;
uint32_t productivity_stopwatch_accumulated = 0U;
bool productivity_timer_running = false;
uint32_t productivity_timer_started = 0U;
uint32_t productivity_timer_seconds = 0U;
bool productivity_timer_expired_reported = false;

bool productivity_ascii_equal_ci(char left, char right) {
    if (left >= 'A' && left <= 'Z') left = static_cast<char>(left + ('a' - 'A'));
    if (right >= 'A' && right <= 'Z') right = static_cast<char>(right + ('a' - 'A'));
    return left == right;
}

bool productivity_contains_ci(const char* text, const char* query) {
    if (!text || !query || !*query) return true;
    const uint32_t text_size = static_cast<uint32_t>(string_length(text));
    const uint32_t query_size = static_cast<uint32_t>(string_length(query));
    if (query_size > text_size) return false;
    for (uint32_t start = 0U; start + query_size <= text_size; ++start) {
        bool match = true;
        for (uint32_t index = 0U; index < query_size; ++index) {
            if (!productivity_ascii_equal_ci(text[start + index], query[index])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

bool productivity_has_suffix(const char* text, const char* suffix) {
    if (!text || !suffix) return false;
    const uint32_t text_size = static_cast<uint32_t>(string_length(text));
    const uint32_t suffix_size = static_cast<uint32_t>(string_length(suffix));
    if (suffix_size > text_size) return false;
    for (uint32_t i = 0U; i < suffix_size; ++i) if (text[text_size - suffix_size + i] != suffix[i]) return false;
    return true;
}

void productivity_append_uint(char* output, uint32_t capacity, uint32_t& length, uint32_t value, uint8_t digits = 0U) {
    char reverse[11]; uint32_t count = 0U;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    while (count < digits && count < sizeof(reverse)) reverse[count++] = '0';
    while (count && length + 1U < capacity) output[length++] = reverse[--count];
    output[length] = 0;
}

bool productivity_append_text(char* output, uint32_t capacity, uint32_t& length, const char* text) {
    while (text && *text) {
        if (length + 1U >= capacity) return false;
        output[length++] = *text++;
    }
    output[length] = 0;
    return true;
}

void productivity_date_text(char output[11], uint16_t year, uint8_t month, uint8_t day) {
    uint32_t length = 0U;
    productivity_append_uint(output, 11U, length, year, 4U);
    if (length + 1U < 11U) output[length++] = '-';
    output[length] = 0;
    productivity_append_uint(output, 11U, length, month, 2U);
    if (length + 1U < 11U) output[length++] = '-';
    output[length] = 0;
    productivity_append_uint(output, 11U, length, day, 2U);
}

void productivity_time_text(char output[6], uint8_t hour, uint8_t minute) {
    uint32_t length = 0U;
    productivity_append_uint(output, 6U, length, hour, 2U);
    if (length + 1U < 6U) output[length++] = ':';
    output[length] = 0;
    productivity_append_uint(output, 6U, length, minute, 2U);
}

bool productivity_ensure_storage() {
    if (productivity_storage_ready) return true;
    char normalized[48]{};
    if (!storage::browser_directory_exists("/notes", normalized)) (void)storage::make_directory("/notes");
    if (!storage::browser_directory_exists("/calendar", normalized)) (void)storage::make_directory("/calendar");
    const bool ready = storage::browser_directory_exists("/notes", normalized) && storage::browser_directory_exists("/calendar", normalized);
    productivity_storage_ready = ready;
    serial::line(ready ? "UI_PRODUCTIVITY_STORAGE_OK" : "UI_PRODUCTIVITY_STORAGE_FAILED");
    return ready;
}

void productivity_notes_rebuild_visible() {
    productivity_note_visible_count = 0U;
    for (uint32_t i = 0U; i < productivity_note_count; ++i) {
        if (!productivity_has_suffix(productivity_notes[i].name, ".md")) continue;
        if (!productivity_contains_ci(productivity_notes[i].name, productivity_note_query)) continue;
        productivity_note_visible[productivity_note_visible_count++] = static_cast<uint8_t>(i);
    }
    if (!productivity_note_visible_count) { productivity_note_focus = 0U; productivity_note_scroll = 0U; return; }
    if (productivity_note_focus >= productivity_note_visible_count) productivity_note_focus = productivity_note_visible_count - 1U;
    if (productivity_note_focus < productivity_note_scroll) productivity_note_scroll = productivity_note_focus;
    if (productivity_note_focus >= productivity_note_scroll + productivity_note_visible_rows)
        productivity_note_scroll = productivity_note_focus - productivity_note_visible_rows + 1U;
}

void productivity_notes_reload() {
    if (!productivity_ensure_storage()) { productivity_note_count = 0U; productivity_note_visible_count = 0U; return; }
    productivity_note_count = storage::list_directory("/notes", productivity_notes, productivity_note_capacity);
    productivity_notes_rebuild_visible();
}

void productivity_note_stem(const char* name, char output[48]) {
    string_copy(output, name ? name : "", 48U);
    const uint32_t length = static_cast<uint32_t>(string_length(output));
    if (length >= 3U && output[length - 3U] == '.' && output[length - 2U] == 'm' && output[length - 1U] == 'd') output[length - 3U] = 0;
}

bool productivity_note_contains_link(const char* text, uint32_t size, const char* stem) {
    char needle[52]{}; uint32_t length = 0U;
    if (!productivity_append_text(needle, sizeof(needle), length, "[[") ||
        !productivity_append_text(needle, sizeof(needle), length, stem) ||
        !productivity_append_text(needle, sizeof(needle), length, "]]")) return false;
    const uint32_t needle_size = length;
    if (needle_size > size) return false;
    for (uint32_t start = 0U; start + needle_size <= size; ++start) {
        bool match = true;
        for (uint32_t i = 0U; i < needle_size; ++i) if (text[start + i] != needle[i]) { match = false; break; }
        if (match) return true;
    }
    return false;
}

void productivity_notes_update_backlinks() {
    productivity_backlink_count = 0U;
    memset(productivity_backlinks, 0, sizeof(productivity_backlinks));
    if (!productivity_note_path[0]) return;
    const char* current_name = productivity_note_path;
    for (const char* cursor = productivity_note_path; *cursor; ++cursor) if (*cursor == '/') current_name = cursor + 1;
    char stem[48]{}; productivity_note_stem(current_name, stem);
    char data[productivity_note_buffer_capacity]{};
    for (uint32_t i = 0U; i < productivity_note_count; ++i) {
        if (string_equal(productivity_notes[i].path, productivity_note_path) || productivity_notes[i].type != storage::zfs_type_file) continue;
        uint32_t size = 0U;
        if (!storage::security_read_file(productivity_notes[i].path, reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) continue;
        data[size] = 0;
        if (!productivity_note_contains_link(data, size, stem)) continue;
        if (productivity_backlink_count < 3U) string_copy(productivity_backlinks[productivity_backlink_count], productivity_notes[i].name, 48U);
        if (productivity_backlink_count < 255U) ++productivity_backlink_count;
    }
}

bool productivity_notes_open_path(const char* path) {
    if (!path) return false;
    uint32_t size = 0U;
    memset(productivity_note_text, 0, sizeof(productivity_note_text));
    if (!storage::security_read_file(path, reinterpret_cast<uint8_t*>(productivity_note_text), sizeof(productivity_note_text) - 1U, size)) return false;
    productivity_note_text[size] = 0;
    productivity_note_size = size;
    string_copy(productivity_note_path, path, sizeof(productivity_note_path));
    productivity_note_editing = true;
    productivity_note_dirty = false;
    productivity_notes_update_backlinks();
    serial::line("UI_NOTES_OPEN_OK");
    return true;
}

bool productivity_notes_save() {
    if (!productivity_note_path[0]) return false;
    const bool saved = storage::guarded_write_file(productivity_note_path,
        reinterpret_cast<const uint8_t*>(productivity_note_text), productivity_note_size, false);
    if (saved) { productivity_note_dirty = false; productivity_notes_reload(); serial::line("UI_NOTES_SAVE_OK"); }
    else serial::line("UI_NOTES_SAVE_FAILED");
    return saved;
}

bool productivity_notes_create_path(const char* path, const char* title, uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
    char text[256]{}; uint32_t length = 0U;
    char date[11]{}; char time[6]{};
    productivity_date_text(date, year, month, day); productivity_time_text(time, hour, minute);
    if (!productivity_append_text(text, sizeof(text), length, "---\ndate: ") ||
        !productivity_append_text(text, sizeof(text), length, date) ||
        !productivity_append_text(text, sizeof(text), length, "\ntags: []\n---\n# ") ||
        !productivity_append_text(text, sizeof(text), length, title) ||
        !productivity_append_text(text, sizeof(text), length, "\n\nCreated ") ||
        !productivity_append_text(text, sizeof(text), length, date) ||
        !productivity_append_text(text, sizeof(text), length, " ") ||
        !productivity_append_text(text, sizeof(text), length, time) ||
        !productivity_append_text(text, sizeof(text), length, "\n")) return false;
    if (!storage::guarded_write_file(path, reinterpret_cast<const uint8_t*>(text), length, false)) return false;
    productivity_notes_reload();
    return productivity_notes_open_path(path);
}

bool productivity_notes_open_daily_for_date(uint16_t year, uint8_t month, uint8_t day) {
    if (!productivity_ensure_storage()) return false;
    char date[11]{}; productivity_date_text(date, year, month, day);
    char path[48]{}; uint32_t length = 0U;
    productivity_append_text(path, sizeof(path), length, "/notes/");
    productivity_append_text(path, sizeof(path), length, date);
    productivity_append_text(path, sizeof(path), length, ".md");
    if (productivity_notes_open_path(path)) return true;
    uint8_t hour = 0U, minute = 0U; (void)rtc_read_time(hour, minute);
    return productivity_notes_create_path(path, date, year, month, day, hour, minute);
}

bool productivity_notes_new() {
    if (!productivity_ensure_storage()) return false;
    uint8_t weekday = 0U, day = 1U, month = 1U, year = 0U, hour = 0U, minute = 0U;
    (void)rtc_read_date(weekday, day, month, year); (void)rtc_read_time(hour, minute);
    char path[48]{}; uint32_t length = 0U;
    productivity_append_text(path, sizeof(path), length, "/notes/note-");
    productivity_append_uint(path, sizeof(path), length, year, 2U);
    productivity_append_uint(path, sizeof(path), length, month, 2U);
    productivity_append_uint(path, sizeof(path), length, day, 2U);
    if (length + 1U < sizeof(path)) path[length++] = '-';
    path[length] = 0;
    productivity_append_uint(path, sizeof(path), length, hour, 2U);
    productivity_append_uint(path, sizeof(path), length, minute, 2U);
    if (length + 1U < sizeof(path)) path[length++] = '-';
    path[length] = 0;
    productivity_append_uint(path, sizeof(path), length, timer_ticks % 100U, 2U);
    productivity_append_text(path, sizeof(path), length, ".md");
    return productivity_notes_create_path(path, "NEW NOTE", static_cast<uint16_t>(2000U + year), month, day, hour, minute);
}

bool productivity_notes_open_scratch() {
    const char* path = "/notes/scratch.txt";
    if (productivity_notes_open_path(path)) return true;
    const char initial[] = "ZENOVOS SCRATCHPAD\n\n";
    if (!storage::guarded_write_file(path, reinterpret_cast<const uint8_t*>(initial), sizeof(initial) - 1U, false)) return false;
    return productivity_notes_open_path(path);
}

uint32_t productivity_note_word_count() {
    uint32_t words = 0U; bool inside = false;
    for (uint32_t i = 0U; i < productivity_note_size; ++i) {
        const char value = productivity_note_text[i];
        const bool separator = value == ' ' || value == '\n' || value == '\r' || value == '\t';
        if (!separator && !inside) { ++words; inside = true; }
        else if (separator) inside = false;
    }
    return words;
}

void productivity_draw_wrapped_text(int32_t x, int32_t y, const char* text, uint32_t size, uint32_t columns, uint32_t rows) {
    uint32_t position = 0U;
    for (uint32_t row = 0U; row < rows && position < size; ++row) {
        char line[73]{}; uint32_t length = 0U;
        while (position < size && length < columns && length + 1U < sizeof(line)) {
            char value = text[position++];
            if (value == '\r') continue;
            if (value == '\n') break;
            if (value == '\t') value = ' ';
            line[length++] = value >= 32 && value <= 126 ? value : '?';
        }
        line[length] = 0;
        draw_text(x, y + static_cast<int32_t>(row * 11U), line, current_theme.text, 1);
    }
}

void productivity_render_notes_browser() {
    productivity_notes_reload();
    draw_window_chrome("ZEN NOTES", "LOCAL MARKDOWN VAULT");
    const int32_t x = window_x + 16, y = window_y + 62;
    fill_round_rect(x, y, 480, 38, 8, current_theme.surface_low);
    draw_char(x + 14, y + 15, '>', current_theme.accent, 1);
    draw_text(x + 32, y + 15, productivity_note_searching ? (productivity_note_query_size ? productivity_note_query : "SEARCH NOTES") : "PRESS / TO SEARCH", productivity_note_searching ? current_theme.text : current_theme.text_muted, 1);
    draw_text_right(x + 462, y + 15, "F1 NEW  F3 SCRATCH  F4 DAILY", current_theme.text_muted, 1);
    fill_round_rect(x, y + 48, 314, 246, 8, current_theme.surface_low);
    draw_text(x + 12, y + 62, "NOTES", current_theme.text_muted, 1);
    for (uint32_t visible = 0U; visible < productivity_note_visible_rows; ++visible) {
        const uint32_t visible_index = productivity_note_scroll + visible;
        if (visible_index >= productivity_note_visible_count) break;
        const uint32_t note_index = productivity_note_visible[visible_index];
        const int32_t row_y = y + 82 + static_cast<int32_t>(visible * 25U);
        if (visible_index == productivity_note_focus) fill_round_rect(x + 8, row_y - 8, 298, 22, 6, current_theme.accent_soft);
        char label[42]{}; string_copy(label, productivity_notes[note_index].name, sizeof(label));
        draw_text(x + 18, row_y, label, visible_index == productivity_note_focus ? current_theme.text : current_theme.text_muted, 1);
    }
    if (!productivity_note_visible_count) draw_text(x + 18, y + 112, "NO MARKDOWN NOTES", current_theme.text_muted, 1);
    fill_round_rect(x + 326, y + 48, 154, 246, 8, current_theme.surface_high);
    draw_text(x + 340, y + 64, "WORKSPACE", current_theme.text_muted, 1);
    draw_text(x + 340, y + 92, "/NOTES", current_theme.accent, 1);
    draw_text(x + 340, y + 124, "LOCAL FILES", current_theme.text, 1);
    draw_text(x + 340, y + 146, "PLAIN MARKDOWN", current_theme.text_muted, 1);
    draw_text(x + 340, y + 178, "SEARCH", current_theme.text, 1);
    draw_text(x + 340, y + 200, "DAILY NOTES", current_theme.text, 1);
    draw_text(x + 340, y + 222, "BACKLINKS", current_theme.text, 1);
    draw_text(x + 340, y + 254, "ENTER OPEN", current_theme.text_muted, 1);
    draw_text(x + 340, y + 276, "DELETE REMOVE", current_theme.text_muted, 1);
    draw_text(x, y + 316, "UP/DOWN SELECT   ENTER OPEN   / SEARCH   ESC CLOSE", current_theme.text_muted, 1);
}

void productivity_render_notes_editor() {
    draw_window_chrome("ZEN NOTES", "PLAIN TEXT EDITOR");
    const int32_t x = window_x + 16, y = window_y + 62;
    fill_round_rect(x, y, 480, 34, 8, current_theme.surface_low);
    draw_text(x + 12, y + 13, productivity_note_path, current_theme.accent, 1);
    draw_text_right(x + 462, y + 13, productivity_note_dirty ? "UNSAVED" : "SAVED", productivity_note_dirty ? current_theme.warning : current_theme.success, 1);
    fill_round_rect(x, y + 44, 480, 238, 8, current_theme.terminal);
    productivity_draw_wrapped_text(x + 12, y + 58, productivity_note_text, productivity_note_size, 72U, 19U);
    draw_text(x, y + 304, "TYPE TO APPEND   BACKSPACE ERASE   F2 SAVE   ESC VAULT", current_theme.text_muted, 1);
    draw_text(x, y + 328, "WORDS", current_theme.text_muted, 1);
    draw_uint(x + 40, y + 328, productivity_note_word_count(), current_theme.text);
    draw_text(x + 118, y + 328, "BACKLINKS", current_theme.text_muted, 1);
    draw_uint(x + 178, y + 328, productivity_backlink_count, productivity_backlink_count ? current_theme.accent : current_theme.text_muted);
    if (productivity_backlink_count) draw_text(x + 230, y + 328, productivity_backlinks[0], current_theme.accent, 1);
}

void productivity_render_notes() {
    if (productivity_note_editing) productivity_render_notes_editor();
    else productivity_render_notes_browser();
}

bool productivity_is_leap(uint16_t year) { return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U; }
uint8_t productivity_days_in_month(uint16_t year, uint8_t month) {
    static constexpr uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) return 30U;
    return month == 2U && productivity_is_leap(year) ? 29U : days[month - 1U];
}
uint8_t productivity_weekday(uint16_t year, uint8_t month, uint8_t day) {
    static constexpr uint8_t table[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    uint16_t adjusted = year;
    if (month < 3U) --adjusted;
    return static_cast<uint8_t>((adjusted + adjusted / 4U - adjusted / 100U + adjusted / 400U + table[month - 1U] + day) % 7U);
}

bool productivity_parse_digits(const char* text, uint32_t count, uint32_t& output) {
    output = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        output = output * 10U + static_cast<uint32_t>(text[i] - '0');
    }
    return true;
}

void productivity_calendar_load() {
    productivity_event_count = 0U;
    if (!productivity_ensure_storage()) return;
    char data[1024]{}; uint32_t size = 0U;
    if (!storage::security_read_file("/calendar/events.db", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) return;
    data[size] = 0;
    uint32_t position = 0U;
    while (position < size && productivity_event_count < productivity_event_capacity) {
        const uint32_t start = position;
        while (position < size && data[position] != '\n') ++position;
        const uint32_t end = position;
        if (position < size) ++position;
        if (end < start + 12U || data[start + 4U] != '-' || data[start + 7U] != '-' || data[start + 10U] != '|') continue;
        uint32_t year = 0U, month = 0U, day = 0U;
        if (!productivity_parse_digits(data + start, 4U, year) || !productivity_parse_digits(data + start + 5U, 2U, month) || !productivity_parse_digits(data + start + 8U, 2U, day)) continue;
        if (year < 2000U || year > 2099U || month < 1U || month > 12U || day < 1U || day > productivity_days_in_month(static_cast<uint16_t>(year), static_cast<uint8_t>(month))) continue;
        ProductivityEvent& event = productivity_events[productivity_event_count++];
        event.year = static_cast<uint16_t>(year); event.month = static_cast<uint8_t>(month); event.day = static_cast<uint8_t>(day);
        uint32_t title = 0U;
        for (uint32_t i = start + 11U; i < end && title + 1U < sizeof(event.title); ++i) {
            const char value = data[i];
            if (value < 32 || value > 126 || value == '|') continue;
            event.title[title++] = value;
        }
        event.title[title] = 0;
    }
}

bool productivity_calendar_save() {
    char data[1024]{}; uint32_t length = 0U;
    for (uint8_t i = 0U; i < productivity_event_count; ++i) {
        const ProductivityEvent& event = productivity_events[i];
        char date[11]{}; productivity_date_text(date, event.year, event.month, event.day);
        if (!productivity_append_text(data, sizeof(data), length, date) || !productivity_append_text(data, sizeof(data), length, "|") ||
            !productivity_append_text(data, sizeof(data), length, event.title) || !productivity_append_text(data, sizeof(data), length, "\n")) return false;
    }
    const bool saved = storage::guarded_write_file("/calendar/events.db", reinterpret_cast<const uint8_t*>(data), length, false);
    serial::line(saved ? "UI_CALENDAR_SAVE_OK" : "UI_CALENDAR_SAVE_FAILED");
    return saved;
}

void productivity_calendar_today() {
    uint8_t weekday = 0U, day = 1U, month = 1U, year = 0U;
    if (rtc_read_date(weekday, day, month, year)) {
        productivity_calendar_year = static_cast<uint16_t>(2000U + year);
        productivity_calendar_month = month; productivity_calendar_day = day;
    }
}

void productivity_calendar_change_month(bool forward) {
    if (forward) {
        if (++productivity_calendar_month > 12U) { productivity_calendar_month = 1U; ++productivity_calendar_year; }
        if (productivity_calendar_year > 2099U) productivity_calendar_year = 2000U;
    } else {
        if (productivity_calendar_month == 1U) { productivity_calendar_month = 12U; productivity_calendar_year = productivity_calendar_year == 2000U ? 2099U : static_cast<uint16_t>(productivity_calendar_year - 1U); }
        else --productivity_calendar_month;
    }
    const uint8_t maximum = productivity_days_in_month(productivity_calendar_year, productivity_calendar_month);
    if (productivity_calendar_day > maximum) productivity_calendar_day = maximum;
}

void productivity_calendar_move_day(int32_t delta) {
    int32_t day = static_cast<int32_t>(productivity_calendar_day) + delta;
    while (day < 1) { productivity_calendar_change_month(false); day += productivity_days_in_month(productivity_calendar_year, productivity_calendar_month); }
    while (day > productivity_days_in_month(productivity_calendar_year, productivity_calendar_month)) { day -= productivity_days_in_month(productivity_calendar_year, productivity_calendar_month); productivity_calendar_change_month(true); }
    productivity_calendar_day = static_cast<uint8_t>(day);
}

uint8_t productivity_events_on(uint16_t year, uint8_t month, uint8_t day) {
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < productivity_event_count; ++i) if (productivity_events[i].year == year && productivity_events[i].month == month && productivity_events[i].day == day) ++count;
    return count;
}

void productivity_calendar_add_event() {
    if (!productivity_event_title_size || productivity_event_count >= productivity_event_capacity) return;
    ProductivityEvent& event = productivity_events[productivity_event_count++];
    event.year = productivity_calendar_year; event.month = productivity_calendar_month; event.day = productivity_calendar_day;
    string_copy(event.title, productivity_event_title, sizeof(event.title));
    productivity_event_title_size = 0U; productivity_event_title[0] = 0; productivity_event_editing = false;
    (void)productivity_calendar_save();
}

void productivity_calendar_delete_event() {
    for (uint8_t i = 0U; i < productivity_event_count; ++i) {
        if (productivity_events[i].year != productivity_calendar_year || productivity_events[i].month != productivity_calendar_month || productivity_events[i].day != productivity_calendar_day) continue;
        for (uint8_t j = i + 1U; j < productivity_event_count; ++j) productivity_events[j - 1U] = productivity_events[j];
        --productivity_event_count; (void)productivity_calendar_save(); return;
    }
}

void productivity_render_calendar() {
    static constexpr const char* months[12] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE","JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
    static constexpr const char* weekdays[7] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    draw_window_chrome("ZEN CALENDAR", "LOCAL EVENTS");
    const int32_t grid_x = window_x + 16, grid_y = window_y + 70;
    fill_round_rect(grid_x, grid_y, 326, 276, 9, current_theme.surface_low);
    draw_text(grid_x + 14, grid_y + 16, months[productivity_calendar_month - 1U], current_theme.text, 2);
    draw_uint(grid_x + 200, grid_y + 16, productivity_calendar_year, current_theme.text_muted);
    for (uint8_t i = 0U; i < 7U; ++i) draw_text(grid_x + 12 + static_cast<int32_t>(i * 44U), grid_y + 48, weekdays[i], current_theme.text_muted, 1);
    const uint8_t first = productivity_weekday(productivity_calendar_year, productivity_calendar_month, 1U);
    const uint8_t count = productivity_days_in_month(productivity_calendar_year, productivity_calendar_month);
    for (uint8_t day = 1U; day <= count; ++day) {
        const uint8_t cell = static_cast<uint8_t>(first + day - 1U);
        const int32_t column = cell % 7U, row = cell / 7U;
        const int32_t x = grid_x + 8 + column * 44, y = grid_y + 66 + row * 32;
        const bool selected = day == productivity_calendar_day;
        if (selected) fill_round_rect(x, y, 36, 26, 6, current_theme.accent_soft);
        draw_uint(x + 10, y + 10, day, selected ? current_theme.text : current_theme.text_muted);
        if (productivity_events_on(productivity_calendar_year, productivity_calendar_month, day)) fill_circle_alpha(x + 28, y + 20, 2, current_theme.accent, 255);
    }
    const int32_t side_x = window_x + 350;
    fill_round_rect(side_x, grid_y, 146, 276, 9, current_theme.surface_high);
    draw_text(side_x + 12, grid_y + 16, "SELECTED", current_theme.text_muted, 1);
    draw_uint(side_x + 12, grid_y + 42, productivity_calendar_day, current_theme.text);
    draw_text(side_x + 40, grid_y + 42, months[productivity_calendar_month - 1U], current_theme.accent, 1);
    uint8_t shown = 0U; int32_t event_y = grid_y + 70;
    for (uint8_t i = 0U; i < productivity_event_count && shown < 4U; ++i) {
        const ProductivityEvent& event = productivity_events[i];
        if (event.year != productivity_calendar_year || event.month != productivity_calendar_month || event.day != productivity_calendar_day) continue;
        fill_round_rect(side_x + 8, event_y, 126, 28, 6, current_theme.surface_low);
        draw_text(side_x + 14, event_y + 11, event.title, current_theme.text, 1);
        event_y += 34; ++shown;
    }
    if (!shown) draw_text(side_x + 12, grid_y + 76, "NO EVENTS", current_theme.text_muted, 1);
    if (productivity_event_editing) {
        fill_round_rect(side_x + 8, grid_y + 226, 126, 34, 7, current_theme.accent_soft);
        draw_text(side_x + 14, grid_y + 240, productivity_event_title_size ? productivity_event_title : "TYPE TITLE", productivity_event_title_size ? current_theme.text : current_theme.text_muted, 1);
    }
    draw_text(window_x + 18, window_y + 364, "ARROWS DAY   F1 PREV   F2 TODAY   F3 NEXT   F4 ADD", current_theme.text_muted, 1);
    draw_text(window_x + 18, window_y + 384, "ENTER DAILY NOTE   DELETE FIRST EVENT", current_theme.text_muted, 1);
}

uint32_t productivity_stopwatch_ticks() {
    return productivity_stopwatch_accumulated + (productivity_stopwatch_running ? static_cast<uint32_t>(timer_ticks - productivity_stopwatch_started) : 0U);
}

uint32_t productivity_timer_remaining() {
    if (!productivity_timer_running) return productivity_timer_seconds;
    const uint32_t elapsed = static_cast<uint32_t>(timer_ticks - productivity_timer_started) / 100U;
    if (elapsed >= productivity_timer_seconds) {
        productivity_timer_running = false; productivity_timer_seconds = 0U;
        if (!productivity_timer_expired_reported) { serial::line("UI_CLOCK_TIMER_EXPIRED"); productivity_timer_expired_reported = true; }
        return 0U;
    }
    return productivity_timer_seconds - elapsed;
}

void productivity_clock_toggle_stopwatch() {
    if (productivity_stopwatch_running) {
        productivity_stopwatch_accumulated += static_cast<uint32_t>(timer_ticks - productivity_stopwatch_started);
        productivity_stopwatch_running = false;
        serial::line("UI_CLOCK_STOPWATCH_PAUSED");
    } else {
        productivity_stopwatch_started = timer_ticks;
        productivity_stopwatch_running = true;
        serial::line("UI_CLOCK_STOPWATCH_RUNNING");
    }
}

void productivity_clock_toggle_timer() {
    if (productivity_timer_running) {
        productivity_timer_seconds = productivity_timer_remaining();
        productivity_timer_running = false;
        serial::line("UI_CLOCK_TIMER_PAUSED");
    } else if (productivity_timer_seconds) {
        productivity_timer_started = timer_ticks;
        productivity_timer_running = true;
        productivity_timer_expired_reported = false;
        serial::line("UI_CLOCK_TIMER_RUNNING");
    }
}

void productivity_draw_duration(int32_t x, int32_t y, uint32_t seconds, uint32_t color, bool tenths, uint32_t raw_ticks = 0U) {
    const uint32_t minutes = seconds / 60U;
    const uint32_t remainder = seconds % 60U;
    draw_two_digits(x, y, static_cast<uint8_t>(minutes > 99U ? 99U : minutes), color);
    draw_char(x + 14, y, ':', current_theme.text_muted, 1);
    draw_two_digits(x + 22, y, static_cast<uint8_t>(remainder), color);
    if (tenths) { draw_char(x + 36, y, '.', current_theme.text_muted, 1); draw_uint(x + 43, y, (raw_ticks / 10U) % 10U, color); }
}

void productivity_render_clock() {
    draw_window_chrome("ZEN CLOCK", "RTC AND TIMERS");
    uint8_t hour = 0U, minute = 0U, weekday = 0U, day = 0U, month = 0U, year = 0U;
    const bool time_ok = rtc_read_time(hour, minute); const bool date_ok = rtc_read_date(weekday, day, month, year);
    const int32_t x = window_x + 16, y = window_y + 66;
    fill_round_rect(x, y, 480, 104, 10, current_theme.surface_low);
    draw_text(x + 18, y + 18, "LOCAL TIME", current_theme.text_muted, 1);
    if (time_ok) {
        draw_two_digits(x + 18, y + 48, hour, current_theme.text); draw_char(x + 32, y + 48, ':', current_theme.text_muted, 1); draw_two_digits(x + 40, y + 48, minute, current_theme.text);
    } else draw_text(x + 18, y + 48, "RTC UNAVAILABLE", current_theme.warning, 1);
    if (date_ok) { char date[11]{}; productivity_date_text(date, static_cast<uint16_t>(2000U + year), month, day); draw_text_right(x + 462, y + 48, date, current_theme.accent, 1); }
    fill_round_rect(x, y + 116, 232, 156, 10, current_theme.surface_high);
    draw_text(x + 16, y + 134, "STOPWATCH", current_theme.text, 1);
    const uint32_t ticks = productivity_stopwatch_ticks();
    productivity_draw_duration(x + 16, y + 172, ticks / 100U, current_theme.accent, true, ticks);
    draw_text(x + 16, y + 214, productivity_stopwatch_running ? "RUNNING" : "PAUSED", productivity_stopwatch_running ? current_theme.success : current_theme.text_muted, 1);
    draw_text(x + 16, y + 242, "F1 START/PAUSE   F2 RESET", current_theme.text_muted, 1);
    fill_round_rect(x + 248, y + 116, 232, 156, 10, current_theme.surface_high);
    draw_text(x + 264, y + 134, "COUNTDOWN", current_theme.text, 1);
    const uint32_t remaining = productivity_timer_remaining();
    productivity_draw_duration(x + 264, y + 172, remaining, productivity_timer_running ? current_theme.accent : current_theme.text, false);
    draw_text(x + 264, y + 214, productivity_timer_running ? "RUNNING" : (remaining ? "READY" : "EMPTY"), productivity_timer_running ? current_theme.success : current_theme.text_muted, 1);
    draw_text(x + 264, y + 242, "F3 +1 MIN   F4 START/PAUSE", current_theme.text_muted, 1);
    draw_text(x + 18, y + 294, "TIME IS READ FROM CMOS RTC. TIMERS USE THE 100 HZ MONOTONIC PIT.", current_theme.text_muted, 1);
    draw_text(x + 18, y + 316, "MOVE THE POINTER OR PRESS A KEY TO REFRESH THE DISPLAY.", current_theme.warning, 1);
}

void productivity_open(ProductivityApp app) {
    if (productivity_note_dirty) (void)productivity_notes_save();
    productivity_overlay_open = true;
    productivity_app = app;
    productivity_note_searching = false;
    productivity_event_editing = false;
    (void)productivity_ensure_storage();
    if (app == ProductivityApp::notes) productivity_notes_reload();
    else if (app == ProductivityApp::calendar) { productivity_calendar_today(); productivity_calendar_load(); }
    serial::line(app == ProductivityApp::notes ? "UI_NOTES_OPEN_APP_OK" : (app == ProductivityApp::calendar ? "UI_CALENDAR_OPEN_APP_OK" : "UI_CLOCK_OPEN_APP_OK"));
}

void render_productivity() {
    if (productivity_app == ProductivityApp::notes) productivity_render_notes();
    else if (productivity_app == ProductivityApp::calendar) productivity_render_calendar();
    else productivity_render_clock();
}

bool productivity_handle_notes_key(uint16_t key) {
    if (productivity_note_searching) {
        if (key == key_escape) { productivity_note_searching = false; productivity_note_query_size = 0U; productivity_note_query[0] = 0; productivity_notes_rebuild_visible(); }
        else if (key == '\b' && productivity_note_query_size) productivity_note_query[--productivity_note_query_size] = 0;
        else if (key == '\n') { productivity_note_searching = false; if (productivity_note_visible_count) (void)productivity_notes_open_path(productivity_notes[productivity_note_visible[productivity_note_focus]].path); }
        else if (key >= 32U && key <= 126U && productivity_note_query_size + 1U < sizeof(productivity_note_query)) { productivity_note_query[productivity_note_query_size++] = static_cast<char>(key); productivity_note_query[productivity_note_query_size] = 0; }
        productivity_notes_rebuild_visible(); return true;
    }
    if (productivity_note_editing) {
        if (key == key_f2) { (void)productivity_notes_save(); return true; }
        if (key == key_escape) { if (productivity_note_dirty) (void)productivity_notes_save(); productivity_note_editing = false; productivity_note_path[0] = 0; productivity_notes_reload(); return true; }
        if (key == '\b') { if (productivity_note_size) { productivity_note_text[--productivity_note_size] = 0; productivity_note_dirty = true; } return true; }
        if (key == key_delete) return true;
        if (key == '\n' || key == '\t' || (key >= 32U && key <= 126U)) {
            if (productivity_note_size + 1U >= sizeof(productivity_note_text)) return true;
            productivity_note_text[productivity_note_size++] = key == '\t' ? ' ' : static_cast<char>(key);
            productivity_note_text[productivity_note_size] = 0; productivity_note_dirty = true; return true;
        }
        return true;
    }
    if (key == key_f1) return productivity_notes_new();
    if (key == key_f2 || key == '\n' || key == ' ') {
        if (productivity_note_visible_count) return productivity_notes_open_path(productivity_notes[productivity_note_visible[productivity_note_focus]].path);
        return true;
    }
    if (key == key_f3) return productivity_notes_open_scratch();
    if (key == key_f4) {
        uint8_t weekday = 0U, day = 1U, month = 1U, year = 0U; (void)rtc_read_date(weekday, day, month, year);
        return productivity_notes_open_daily_for_date(static_cast<uint16_t>(2000U + year), month, day);
    }
    if (key == '/') { productivity_note_searching = true; productivity_note_query_size = 0U; productivity_note_query[0] = 0; return true; }
    if (key == key_delete) {
        if (productivity_note_visible_count) { const char* path = productivity_notes[productivity_note_visible[productivity_note_focus]].path; if (storage::guarded_remove(path)) { productivity_notes_reload(); serial::line("UI_NOTES_DELETE_OK"); } }
        return true;
    }
    if (!productivity_note_visible_count) return key != key_escape;
    if (key == key_up) productivity_note_focus = productivity_note_focus == 0U ? productivity_note_visible_count - 1U : productivity_note_focus - 1U;
    else if (key == key_down || key == key_right || key == '\t') productivity_note_focus = (productivity_note_focus + 1U) % productivity_note_visible_count;
    else if (key == key_home) productivity_note_focus = 0U;
    else if (key == key_end) productivity_note_focus = productivity_note_visible_count - 1U;
    else return key != key_escape;
    productivity_notes_rebuild_visible(); return true;
}

bool productivity_handle_calendar_key(uint16_t key) {
    if (productivity_event_editing) {
        if (key == key_escape) { productivity_event_editing = false; productivity_event_title_size = 0U; productivity_event_title[0] = 0; return true; }
        if (key == '\b' && productivity_event_title_size) productivity_event_title[--productivity_event_title_size] = 0;
        else if (key == '\n') productivity_calendar_add_event();
        else if (key >= 32U && key <= 126U && key != '|' && productivity_event_title_size + 1U < sizeof(productivity_event_title)) {
            productivity_event_title[productivity_event_title_size++] = static_cast<char>(key); productivity_event_title[productivity_event_title_size] = 0;
        }
        return true;
    }
    if (key == key_f1) productivity_calendar_change_month(false);
    else if (key == key_f2) productivity_calendar_today();
    else if (key == key_f3) productivity_calendar_change_month(true);
    else if (key == key_f4) { productivity_event_editing = true; productivity_event_title_size = 0U; productivity_event_title[0] = 0; }
    else if (key == key_left) productivity_calendar_move_day(-1);
    else if (key == key_right) productivity_calendar_move_day(1);
    else if (key == key_up) productivity_calendar_move_day(-7);
    else if (key == key_down) productivity_calendar_move_day(7);
    else if (key == key_delete) productivity_calendar_delete_event();
    else if (key == '\n' || key == ' ') {
        productivity_open(ProductivityApp::notes);
        (void)productivity_notes_open_daily_for_date(productivity_calendar_year, productivity_calendar_month, productivity_calendar_day);
    } else return key != key_escape;
    return true;
}

bool productivity_handle_clock_key(uint16_t key) {
    if (key == key_f1 || key == ' ') productivity_clock_toggle_stopwatch();
    else if (key == key_f2) { productivity_stopwatch_running = false; productivity_stopwatch_accumulated = 0U; }
    else if (key == key_f3) {
        const uint32_t remaining = productivity_timer_remaining();
        productivity_timer_seconds = remaining > 3540U ? 3600U : remaining + 60U;
        productivity_timer_running = false;
        productivity_timer_expired_reported = false;
        serial::line("UI_CLOCK_TIMER_SET_OK");
    }
    else if (key == key_f4 || key == '\n') productivity_clock_toggle_timer();
    else return key != key_escape;
    return true;
}

bool productivity_handle_navigation_key(uint16_t key) {
    if (productivity_app == ProductivityApp::notes) return productivity_handle_notes_key(key);
    if (productivity_app == ProductivityApp::calendar) return productivity_handle_calendar_key(key);
    return productivity_handle_clock_key(key);
}

void productivity_close() {
    if (productivity_note_dirty) (void)productivity_notes_save();
    productivity_note_searching = false;
    productivity_event_editing = false;
    productivity_overlay_open = false;
    serial::line("UI_PRODUCTIVITY_CLOSE_OK");
}

void render_scene() {
    if (!productivity_overlay_open) { render_scene_base(); return; }
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    const int32_t saved_window_y = window_y;
    window_y += ui_scene_offset_y;
    render_productivity();
    window_y = saved_window_y;
    draw_taskbar();
    draw_quick_settings();
    draw_status_center();
}

bool init() {
    const bool initialized = init_files_base();
    if (initialized) serial::line("UI_PRODUCTIVITY_APPS_READY notes=yes calendar=yes clock=yes scratchpad=yes");
    return initialized;
}

bool handle_navigation_key(uint16_t key) {
    if (!productivity_overlay_open) {
        if (key == key_f1) { productivity_open(ProductivityApp::notes); refresh_desktop(); return true; }
        if (key == key_f2) { productivity_open(ProductivityApp::calendar); refresh_desktop(); return true; }
        if (key == key_f3) { productivity_open(ProductivityApp::clock); refresh_desktop(); return true; }
        return handle_navigation_key_files_base(key);
    }

    if (key == key_f5 || key == key_f6 || key == key_f7 || key == key_f8 || key == key_f9 || key == key_f10 || key == key_f11 || key == key_super) {
        productivity_close();
        return handle_navigation_key_files_base(key);
    }
    if (productivity_handle_navigation_key(key)) { refresh_desktop(); return true; }
    if (key == key_escape) { productivity_close(); refresh_desktop(); return true; }
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    if (!productivity_overlay_open) { on_mouse_packet_files_base(delta_x, delta_y, buttons); return; }
    if (!active) return;
    restore_cursor(mouse_screen_x, mouse_screen_y);
    mouse_screen_x += delta_x; mouse_screen_y += delta_y;
    if (mouse_screen_x < 0) mouse_screen_x = 0;
    if (mouse_screen_y < 0) mouse_screen_y = 0;
    if (mouse_screen_x >= static_cast<int32_t>(display_width)) mouse_screen_x = static_cast<int32_t>(display_width) - 1;
    if (mouse_screen_y >= static_cast<int32_t>(display_height)) mouse_screen_y = static_cast<int32_t>(display_height) - 1;
    update_logical_mouse();
    const bool left = (buttons & 1U) != 0U;
    const bool previous_left = (mouse_buttons & 1U) != 0U;
    if (left && !previous_left) {
        if (!hit(mouse_x, mouse_y, window_x, window_y, window_width, window_height)) {
            productivity_close(); refresh_desktop(); mouse_buttons = buttons; return;
        }
        if (mouse_y < window_y + titlebar_height) {
            dragging = true; drag_offset_x = mouse_x - window_x; drag_offset_y = mouse_y - window_y;
        }
    }
    if (!left) dragging = false;
    if (dragging) {
        window_x = mouse_x - drag_offset_x; window_y = mouse_y - drag_offset_y;
        if (window_x < 24) window_x = 24;
        if (window_x > static_cast<int32_t>(width) - window_width - 24) window_x = static_cast<int32_t>(width) - window_width - 24;
        if (window_y < 50) window_y = 50;
        if (window_y > 116) window_y = 116;
        refresh_desktop();
    } else if (productivity_app == ProductivityApp::clock) refresh_desktop();
    else draw_cursor();
    mouse_buttons = buttons;
    if (first_mouse_packet) { serial::line("MOUSE_PACKET_OK"); first_mouse_packet = false; }
}
