namespace productivity_model {

constexpr unsigned int editor_capacity = 2048U;

struct Date {
    unsigned int year;
    unsigned char month;
    unsigned char day;
};

bool leap_year(unsigned int year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

unsigned char days_in_month(unsigned int year, unsigned char month) {
    static constexpr unsigned char days[12] = {31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && leap_year(year)) return 29U;
    return days[month - 1U];
}

bool valid_date(const Date& date) {
    const unsigned char maximum = days_in_month(date.year, date.month);
    return date.year >= 1970U && date.year <= 9999U && maximum && date.day >= 1U && date.day <= maximum;
}

unsigned char weekday_monday_zero(const Date& date) {
    if (!valid_date(date)) return 0U;
    static constexpr unsigned char offsets[12] = {0U,3U,2U,5U,0U,3U,5U,1U,4U,6U,2U,4U};
    unsigned int year = date.year;
    if (date.month < 3U) --year;
    const unsigned int sunday_zero = (year + year / 4U - year / 100U + year / 400U + offsets[date.month - 1U] + date.day) % 7U;
    return static_cast<unsigned char>((sunday_zero + 6U) % 7U);
}

bool shift_month(Date& date, int direction) {
    if (!valid_date(date) || (direction != -1 && direction != 1)) return false;
    if (direction > 0) {
        if (date.month == 12U) { if (date.year == 9999U) return false; date.month = 1U; ++date.year; }
        else ++date.month;
    } else {
        if (date.month == 1U) { if (date.year == 1970U) return false; date.month = 12U; --date.year; }
        else --date.month;
    }
    const unsigned char maximum = days_in_month(date.year, date.month);
    if (date.day > maximum) date.day = maximum;
    return true;
}

bool shift_day(Date& date, int direction) {
    if (!valid_date(date) || (direction != -1 && direction != 1)) return false;
    if (direction > 0) {
        if (date.day < days_in_month(date.year, date.month)) { ++date.day; return true; }
        date.day = 1U;
        return shift_month(date, 1);
    }
    if (date.day > 1U) { --date.day; return true; }
    if (!shift_month(date, -1)) return false;
    date.day = days_in_month(date.year, date.month);
    return true;
}

bool shift_days(Date& date, int delta) {
    if (delta < -366 || delta > 366) return false;
    while (delta > 0) { if (!shift_day(date, 1)) return false; --delta; }
    while (delta < 0) { if (!shift_day(date, -1)) return false; ++delta; }
    return true;
}

struct Editor {
    char data[editor_capacity]{};
    unsigned int length = 0U;
    unsigned int cursor = 0U;
    bool dirty = false;
};

void editor_clear(Editor& editor) {
    editor.length = 0U; editor.cursor = 0U; editor.dirty = false; editor.data[0] = 0;
}

bool editor_load(Editor& editor, const unsigned char* data, unsigned int size) {
    if ((!data && size) || size >= editor_capacity) return false;
    editor_clear(editor);
    for (unsigned int i = 0U; i < size; ++i) {
        const unsigned char value = data[i];
        if (value == 0U || (value < 32U && value != '\n' && value != '\t' && value != '\r')) return false;
        if (value == '\r') continue;
        editor.data[editor.length++] = value == '\t' ? ' ' : static_cast<char>(value);
    }
    editor.data[editor.length] = 0;
    editor.cursor = editor.length;
    editor.dirty = false;
    return true;
}

bool editor_insert(Editor& editor, char value) {
    if ((value < 32 || value > 126) && value != '\n') return false;
    if (editor.length + 1U >= editor_capacity || editor.cursor > editor.length) return false;
    for (unsigned int i = editor.length; i > editor.cursor; --i) editor.data[i] = editor.data[i - 1U];
    editor.data[editor.cursor++] = value;
    ++editor.length;
    editor.data[editor.length] = 0;
    editor.dirty = true;
    return true;
}

bool editor_backspace(Editor& editor) {
    if (!editor.cursor || editor.cursor > editor.length) return false;
    for (unsigned int i = editor.cursor - 1U; i < editor.length; ++i) editor.data[i] = editor.data[i + 1U];
    --editor.cursor; --editor.length; editor.dirty = true;
    return true;
}

bool editor_delete(Editor& editor) {
    if (editor.cursor >= editor.length) return false;
    for (unsigned int i = editor.cursor; i < editor.length; ++i) editor.data[i] = editor.data[i + 1U];
    --editor.length; editor.dirty = true;
    return true;
}

void editor_move_horizontal(Editor& editor, int direction) {
    if (direction < 0 && editor.cursor) --editor.cursor;
    else if (direction > 0 && editor.cursor < editor.length) ++editor.cursor;
}

unsigned int editor_line_start(const Editor& editor, unsigned int position) {
    if (position > editor.length) position = editor.length;
    while (position && editor.data[position - 1U] != '\n') --position;
    return position;
}

unsigned int editor_line_end(const Editor& editor, unsigned int position) {
    if (position > editor.length) position = editor.length;
    while (position < editor.length && editor.data[position] != '\n') ++position;
    return position;
}

void editor_move_line(Editor& editor, int direction) {
    if (direction == 0) return;
    const unsigned int start = editor_line_start(editor, editor.cursor);
    const unsigned int column = editor.cursor - start;
    if (direction < 0) {
        if (!start) return;
        const unsigned int previous_end = start - 1U;
        const unsigned int previous_start = editor_line_start(editor, previous_end);
        const unsigned int previous_length = previous_end - previous_start;
        editor.cursor = previous_start + (column < previous_length ? column : previous_length);
        return;
    }
    const unsigned int end = editor_line_end(editor, editor.cursor);
    if (end >= editor.length) return;
    const unsigned int next_start = end + 1U;
    const unsigned int next_end = editor_line_end(editor, next_start);
    const unsigned int next_length = next_end - next_start;
    editor.cursor = next_start + (column < next_length ? column : next_length);
}

void editor_home(Editor& editor) { editor.cursor = editor_line_start(editor, editor.cursor); }
void editor_end(Editor& editor) { editor.cursor = editor_line_end(editor, editor.cursor); }

unsigned int count_wiki_links(const Editor& editor) {
    unsigned int count = 0U;
    for (unsigned int i = 0U; i + 1U < editor.length; ++i) if (editor.data[i] == '[' && editor.data[i + 1U] == '[') { ++count; ++i; }
    return count;
}

unsigned int count_tags(const Editor& editor) {
    unsigned int count = 0U;
    for (unsigned int i = 0U; i < editor.length; ++i) {
        if (editor.data[i] != '#') continue;
        const bool boundary = i == 0U || editor.data[i - 1U] == ' ' || editor.data[i - 1U] == '\n';
        const bool value = i + 1U < editor.length && ((editor.data[i + 1U] >= 'A' && editor.data[i + 1U] <= 'Z') ||
            (editor.data[i + 1U] >= 'a' && editor.data[i + 1U] <= 'z') ||
            (editor.data[i + 1U] >= '0' && editor.data[i + 1U] <= '9'));
        if (boundary && value) ++count;
    }
    return count;
}

} // namespace productivity_model
