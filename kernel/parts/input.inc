uint32_t history_count = 0;

constexpr const char* builtin_commands[] = {
    "help", "home", "info", "system", "status", "devices", "disk", "mount", "df", "ver", "version", "uname",
    "cpu", "mem", "memmap", "uptime", "ticks", "date", "time", "echo", "calc", "theme", "color",
    "pwd", "cd", "ls", "files", "dir", "cat", "open", "view", "mkdir", "touch", "write", "append",
    "rm", "del", "cp", "copy", "mv", "ren", "stat", "apps", "run", "history", "bootlog", "clear", "cls",
    "reboot", "halt", "shutdown", "panic"
};

uint32_t common_prefix_length(const char* left, const char* right) {
    uint32_t length = 0;
    while (left[length] && right[length] && left[length] == right[length]) ++length;
    return length;
}
void render_input(const char* buffer, uint32_t length, uint32_t cursor, uint32_t origin_row, uint32_t origin_column, uint32_t& painted) {
    const uint32_t clear_to = painted > length ? painted : length;
    for (uint32_t i = 0; i < clear_to && origin_column + i < console::width; ++i) {
        const char value = i < length ? buffer[i] : ' ';
        console::vga[origin_row * console::width + origin_column + i] = console::styled_cell(value, console::palette.body_fg, console::palette.body_bg);
    }
    painted = length;
    console::move_to(origin_row, origin_column + cursor);
}
void history_add(const char* line) {
    if (!*line) return;
    if (history_count && string_equal(history[(history_count - 1) % 16], line)) return;
    string_copy(history[history_count % 16], line, 80);
    ++history_count;
}
void replace_input(char* buffer, uint32_t& length, uint32_t& cursor, const char* replacement,
                   uint32_t origin_row, uint32_t origin_column, uint32_t& painted, uint32_t maximum) {
    string_copy(buffer, replacement, maximum + 1);
    length = static_cast<uint32_t>(string_length(buffer));
    if (length > maximum) { length = maximum; buffer[length] = 0; }
    cursor = length;
    render_input(buffer, length, cursor, origin_row, origin_column, painted);
}
void complete_input(char* buffer, uint32_t& length, uint32_t& cursor,
                    uint32_t origin_row, uint32_t origin_column, uint32_t& painted, uint32_t maximum) {
    if (cursor != length || length == 0) return;
    for (uint32_t i = 0; i < length; ++i) if (is_space(buffer[i])) return;
    char prefix[80]; string_copy(prefix, buffer, sizeof(prefix)); lower_in_place(prefix);
    char completion[80]; completion[0] = 0;
    uint32_t matches = 0;
    uint32_t shared = 0;
    const uint32_t builtin_count = static_cast<uint32_t>(sizeof(builtin_commands) / sizeof(builtin_commands[0]));
    for (uint32_t i = 0; i < builtin_count; ++i) {
        const char* candidate = builtin_commands[i];
        if (!string_starts_with(candidate, prefix)) continue;
        if (matches == 0) { string_copy(completion, candidate, sizeof(completion)); shared = static_cast<uint32_t>(string_length(candidate)); }
        else { const uint32_t common = common_prefix_length(completion, candidate); if (common < shared) shared = common; }
        ++matches;
    }
    for (uint32_t i = 0; i < zenov_generated::kStaticCommandCount; ++i) {
        const char* candidate = zenov_generated::kStaticCommands[i].name;
        if (!string_starts_with(candidate, prefix)) continue;
        if (matches == 0) { string_copy(completion, candidate, sizeof(completion)); shared = static_cast<uint32_t>(string_length(candidate)); }
        else { const uint32_t common = common_prefix_length(completion, candidate); if (common < shared) shared = common; }
        ++matches;
    }
    if (matches == 0 || shared <= length) return;
    const uint32_t target = matches == 1 && shared + 1 <= maximum ? shared + 1 : shared;
    for (uint32_t i = 0; i < shared; ++i) buffer[i] = completion[i];
    if (target > shared) buffer[shared] = ' ';
    buffer[target] = 0;
    length = target;
    cursor = target;
    render_input(buffer, length, cursor, origin_row, origin_column, painted);
}
const char* shortcut_command(uint16_t key) {
    if (key == key_f1) return "help";
    if (key == key_f2) return "info";
    if (key == key_f3) return "ls";
    if (key == key_f4) return "home";
    return nullptr;
}
void read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;
    uint32_t cursor = 0;
    uint32_t painted = 0;
    uint32_t navigation = history_count;
    const uint32_t origin_row = console::row;
    const uint32_t origin_column = console::column;
    const uint32_t screen_limit = origin_column + 1 < console::width ? console::width - origin_column - 1 : 0;
    const uint32_t capacity_limit = capacity > 0 ? capacity - 1 : 0;
    const uint32_t maximum = screen_limit < capacity_limit ? screen_limit : capacity_limit;
    char draft[80]; draft[0] = 0;
    bool draft_saved = false;
    buffer[0] = 0;
    for (;;) {
        const uint16_t key = key_pop_blocking();
        const char* shortcut = shortcut_command(key);
        if (shortcut && length == 0) {
            replace_input(buffer, length, cursor, shortcut, origin_row, origin_column, painted, maximum);
            serial::write(buffer);
            console::move_to(origin_row, origin_column + length);
            console::put('\n');
            history_add(buffer);
            return;
        }
        if (key == '\n') {
            serial::write(buffer);
            console::move_to(origin_row, origin_column + length);
            console::put('\n');
            buffer[length] = 0;
            history_add(buffer);
            return;
        }
        if (key == key_escape) {
            length = 0; cursor = 0; buffer[0] = 0;
            render_input(buffer, length, cursor, origin_row, origin_column, painted);
            navigation = history_count;
            draft_saved = false;
            continue;
        }
        if (key == '\b') {
            if (cursor) {
                for (uint32_t i = cursor - 1; i < length; ++i) buffer[i] = buffer[i + 1];
                --cursor; --length;
                render_input(buffer, length, cursor, origin_row, origin_column, painted);
            }
            continue;
        }
        if (key == key_delete) {
            if (cursor < length) {
                for (uint32_t i = cursor; i < length; ++i) buffer[i] = buffer[i + 1];
                --length;
                render_input(buffer, length, cursor, origin_row, origin_column, painted);
            }
            continue;
        }
        if (key == key_left) { if (cursor) { --cursor; console::move_to(origin_row, origin_column + cursor); } continue; }
        if (key == key_right) { if (cursor < length) { ++cursor; console::move_to(origin_row, origin_column + cursor); } continue; }
        if (key == key_home) { cursor = 0; console::move_to(origin_row, origin_column); continue; }
        if (key == key_end) { cursor = length; console::move_to(origin_row, origin_column + cursor); continue; }
        if (key == '\t') { complete_input(buffer, length, cursor, origin_row, origin_column, painted, maximum); continue; }
        if (key == key_up) {
            const uint32_t available = history_count < 16 ? history_count : 16;
            const uint32_t oldest = history_count - available;
            if (!draft_saved) { string_copy(draft, buffer, sizeof(draft)); draft_saved = true; }
            if (navigation > oldest) {
                --navigation;
                replace_input(buffer, length, cursor, history[navigation % 16], origin_row, origin_column, painted, maximum);
            }
            continue;
        }
        if (key == key_down) {
            if (navigation < history_count) {
                ++navigation;
                replace_input(buffer, length, cursor, navigation == history_count ? draft : history[navigation % 16],
                              origin_row, origin_column, painted, maximum);
            }
            continue;
        }
        if (key >= 32 && key <= 126 && length < maximum) {
            for (uint32_t i = length; i > cursor; --i) buffer[i] = buffer[i - 1];
            buffer[cursor++] = static_cast<char>(key);
            ++length;
            buffer[length] = 0;
            render_input(buffer, length, cursor, origin_row, origin_column, painted);
        }
    }
}

bool parse_i32(const char*& text, int32_t& value) {
    while (is_space(*text)) ++text;
    bool negative = false;
    if (*text == '-' || *text == '+') { negative = *text == '-'; ++text; }
    if (*text < '0' || *text > '9') return false;
    uint32_t result = 0;
    while (*text >= '0' && *text <= '9') { result = result * 10U + static_cast<uint32_t>(*text - '0'); ++text; }
    value = negative ? -static_cast<int32_t>(result) : static_cast<int32_t>(result);
    return true;
}
