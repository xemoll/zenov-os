uint32_t history_count = 0;

void replace_input(char* buffer, uint32_t& length, const char* replacement) {
    while (length) { console::put('\b'); --length; }
    string_copy(buffer, replacement, 80);
    length = static_cast<uint32_t>(string_length(buffer));
    console::write(buffer);
}
void history_add(const char* line) {
    if (!*line) return;
    if (history_count && string_equal(history[(history_count - 1) % 8], line)) return;
    string_copy(history[history_count % 8], line, 80);
    ++history_count;
}
void read_line(char* buffer, uint32_t capacity) {
    uint32_t length = 0;
    uint32_t navigation = history_count;
    buffer[0] = 0;
    for (;;) {
        const uint16_t key = key_pop_blocking();
        if (key == '\n') { console::put('\n'); buffer[length] = 0; history_add(buffer); return; }
        if (key == '\b') { if (length) { --length; buffer[length] = 0; console::put('\b'); } continue; }
        if (key == key_up) {
            const uint32_t available = history_count < 8 ? history_count : 8;
            const uint32_t oldest = history_count - available;
            if (navigation > oldest) { --navigation; replace_input(buffer, length, history[navigation % 8]); }
            continue;
        }
        if (key == key_down) {
            if (navigation < history_count) {
                ++navigation;
                replace_input(buffer, length, navigation == history_count ? "" : history[navigation % 8]);
            }
            continue;
        }
        if (key >= 32 && key <= 126 && length + 1 < capacity) {
            buffer[length++] = static_cast<char>(key); buffer[length] = 0; console::put(static_cast<char>(key));
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

