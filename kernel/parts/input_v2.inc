static_assert(zenov_generated::kShellLineCapacity >= 128 && zenov_generated::kShellLineCapacity <= 1024);
static_assert(zenov_generated::kShellHistoryCapacity >= 16 && zenov_generated::kShellHistoryCapacity <= 256);
static_assert(zenov_generated::kShellLineCapacity * zenov_generated::kShellHistoryCapacity <= 128U * 1024U);

char shell_history[zenov_generated::kShellHistoryCapacity][zenov_generated::kShellLineCapacity];
uint32_t shell_history_count = 0;

constexpr const char* builtin_commands[] = {
    "help", "home", "info", "system", "status", "devices", "disk", "mount", "df", "fsck", "sync",
    "ver", "version", "uname", "cpu", "mem", "pmm", "vm", "memmap", "uptime", "ticks", "date", "time",
    "echo", "calc", "theme", "color", "pwd", "cd", "ls", "files", "dir", "cat", "open", "view",
    "mkdir", "touch", "write", "append", "rm", "del", "cp", "copy", "mv", "ren", "stat", "apps", "run",
    "history", "bootlog", "clear", "cls", "reboot", "halt", "shutdown", "panic"
};

uint32_t common_prefix_length(const char* left, const char* right) {
    uint32_t length = 0;
    while (left[length] && right[length] && left[length] == right[length]) ++length;
    return length;
}

uint32_t visible_input_width(uint32_t origin_column) {
    return origin_column < console::width ? console::width - origin_column : 0;
}

void keep_cursor_visible(uint32_t cursor, uint32_t visible, uint32_t& view_start) {
    if (visible == 0) { view_start = cursor; return; }
    if (cursor < view_start) view_start = cursor;
    else if (cursor >= view_start + visible) view_start = cursor - visible + 1;
}

void render_input(const char* buffer, uint32_t length, uint32_t cursor,
                  uint32_t origin_row, uint32_t origin_column, uint32_t& view_start) {
    const uint32_t visible = visible_input_width(origin_column);
    keep_cursor_visible(cursor, visible, view_start);
    for (uint32_t i = 0; i < visible; ++i) {
        const uint32_t source = view_start + i;
        const char value = source < length ? buffer[source] : ' ';
        console::vga[origin_row * console::width + origin_column + i] =
            console::styled_cell(value, console::palette.body_fg, console::palette.body_bg);
    }
    const uint32_t cursor_column = visible == 0 ? origin_column : origin_column + cursor - view_start;
    console::move_to(origin_row, cursor_column < console::width ? cursor_column : console::width - 1);
    graphics::sync_terminal_from_console();
}

void history_add(const char* line) {
    if (!*line) return;
    const uint32_t capacity = zenov_generated::kShellHistoryCapacity;
    if (shell_history_count && string_equal(shell_history[(shell_history_count - 1) % capacity], line)) return;
    string_copy(shell_history[shell_history_count % capacity], line, zenov_generated::kShellLineCapacity);
    ++shell_history_count;
}

void replace_input(char* buffer, uint32_t& length, uint32_t& cursor, const char* replacement,
                   uint32_t origin_row, uint32_t origin_column, uint32_t& view_start, uint32_t maximum) {
    string_copy(buffer, replacement, maximum + 1);
    length = static_cast<uint32_t>(string_length(buffer));
    if (length > maximum) { length = maximum; buffer[length] = 0; }
    cursor = length;
    render_input(buffer, length, cursor, origin_row, origin_column, view_start);
}

void complete_input(char* buffer, uint32_t& length, uint32_t& cursor,
                    uint32_t origin_row, uint32_t origin_column, uint32_t& view_start, uint32_t maximum) {
    if (cursor != length || length == 0) return;
    for (uint32_t i = 0; i < length; ++i) if (is_space(buffer[i])) return;
    char prefix[zenov_generated::kShellLineCapacity];
    string_copy(prefix, buffer, sizeof(prefix));
    lower_in_place(prefix);
    char completion[zenov_generated::kShellLineCapacity];
    completion[0] = 0;
    uint32_t matches = 0;
    uint32_t shared = 0;
    const uint32_t builtin_count = static_cast<uint32_t>(sizeof(builtin_commands) / sizeof(builtin_commands[0]));
    for (uint32_t i = 0; i < builtin_count; ++i) {
        const char* candidate = builtin_commands[i];
        if (!string_starts_with(candidate, prefix)) continue;
        if (matches == 0) {
            string_copy(completion, candidate, sizeof(completion));
            shared = static_cast<uint32_t>(string_length(candidate));
        } else {
            const uint32_t common = common_prefix_length(completion, candidate);
            if (common < shared) shared = common;
        }
        ++matches;
    }
    for (uint32_t i = 0; i < zenov_generated::kStaticCommandCount; ++i) {
        const char* candidate = zenov_generated::kStaticCommands[i].name;
        if (!string_starts_with(candidate, prefix)) continue;
        if (matches == 0) {
            string_copy(completion, candidate, sizeof(completion));
            shared = static_cast<uint32_t>(string_length(candidate));
        } else {
            const uint32_t common = common_prefix_length(completion, candidate);
            if (common < shared) shared = common;
        }
        ++matches;
    }
    if (matches == 0 || shared <= length || shared > maximum) return;
    const uint32_t target = matches == 1 && shared + 1 <= maximum ? shared + 1 : shared;
    for (uint32_t i = 0; i < shared; ++i) buffer[i] = completion[i];
    if (target > shared) buffer[shared] = ' ';
    buffer[target] = 0;
    length = target;
    cursor = target;
    render_input(buffer, length, cursor, origin_row, origin_column, view_start);
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
    uint32_t view_start = 0;
    uint32_t navigation = shell_history_count;
    const uint32_t origin_row = console::row;
    const uint32_t origin_column = console::column;
    const uint32_t maximum = capacity > 0 ? capacity - 1 : 0;
    char draft[zenov_generated::kShellLineCapacity];
    draft[0] = 0;
    bool draft_saved = false;
    buffer[0] = 0;
    for (;;) {
        const uint16_t key = key_pop_blocking();
        const char* shortcut = shortcut_command(key);
        if (shortcut && length == 0) {
            replace_input(buffer, length, cursor, shortcut, origin_row, origin_column, view_start, maximum);
            serial::write(buffer);
            console::put('\n');
            history_add(buffer);
            return;
        }
        if (key == '\n') {
            serial::write(buffer);
            console::put('\n');
            buffer[length] = 0;
            history_add(buffer);
            return;
        }
        if (key == key_escape) {
            length = 0; cursor = 0; view_start = 0; buffer[0] = 0;
            render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            navigation = shell_history_count;
            draft_saved = false;
            continue;
        }
        if (key == '\b') {
            if (cursor) {
                for (uint32_t i = cursor - 1; i < length; ++i) buffer[i] = buffer[i + 1];
                --cursor; --length;
                render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            }
            continue;
        }
        if (key == key_delete) {
            if (cursor < length) {
                for (uint32_t i = cursor; i < length; ++i) buffer[i] = buffer[i + 1];
                --length;
                render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            }
            continue;
        }
        if (key == key_left) {
            if (cursor) --cursor;
            render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            continue;
        }
        if (key == key_right) {
            if (cursor < length) ++cursor;
            render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            continue;
        }
        if (key == key_home) {
            cursor = 0;
            render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            continue;
        }
        if (key == key_end) {
            cursor = length;
            render_input(buffer, length, cursor, origin_row, origin_column, view_start);
            continue;
        }
        if (key == '\t') {
            complete_input(buffer, length, cursor, origin_row, origin_column, view_start, maximum);
            continue;
        }
        if (key == key_up) {
            const uint32_t capacity_count = zenov_generated::kShellHistoryCapacity;
            const uint32_t available = shell_history_count < capacity_count ? shell_history_count : capacity_count;
            const uint32_t oldest = shell_history_count - available;
            if (!draft_saved) { string_copy(draft, buffer, sizeof(draft)); draft_saved = true; }
            if (navigation > oldest) {
                --navigation;
                replace_input(buffer, length, cursor, shell_history[navigation % capacity_count],
                              origin_row, origin_column, view_start, maximum);
            }
            continue;
        }
        if (key == key_down) {
            if (navigation < shell_history_count) {
                ++navigation;
                const uint32_t capacity_count = zenov_generated::kShellHistoryCapacity;
                replace_input(buffer, length, cursor,
                              navigation == shell_history_count ? draft : shell_history[navigation % capacity_count],
                              origin_row, origin_column, view_start, maximum);
            }
            continue;
        }
        if (key >= 32 && key <= 126 && length < maximum) {
            for (uint32_t i = length; i > cursor; --i) buffer[i] = buffer[i - 1];
            buffer[cursor++] = static_cast<char>(key);
            ++length;
            buffer[length] = 0;
            render_input(buffer, length, cursor, origin_row, origin_column, view_start);
        }
    }
}

bool parse_i32(const char*& text, int32_t& value) {
    while (is_space(*text)) ++text;
    bool negative = false;
    if (*text == '-' || *text == '+') { negative = *text == '-'; ++text; }
    if (*text < '0' || *text > '9') return false;
    uint32_t result = 0;
    while (*text >= '0' && *text <= '9') {
        result = result * 10U + static_cast<uint32_t>(*text - '0');
        ++text;
    }
    value = negative ? -static_cast<int32_t>(result) : static_cast<int32_t>(result);
    return true;
}
