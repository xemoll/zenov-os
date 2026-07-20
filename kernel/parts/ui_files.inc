constexpr uint32_t files_capacity = 32U;
constexpr uint32_t files_visible_rows = 7U;
constexpr int32_t files_sidebar_x_offset = 12;
constexpr int32_t files_sidebar_width = 116;
constexpr int32_t files_content_x_offset = 144;
constexpr int32_t files_content_width = 352;
constexpr int32_t files_row_y_offset = 128;
constexpr int32_t files_row_height = 30;

storage::BrowserEntry files_entries[files_capacity]{};
uint32_t files_entry_count = 0;
uint32_t files_focus = 0;
uint32_t files_scroll = 0;
int32_t files_hover_row = -1;
uint8_t files_hover_place = 0xFFU;
char files_path[48] = "/";
bool files_preview_open = false;
bool files_preview_binary = false;
bool files_navigation_reported = false;
bool files_preview_reported = false;
char files_preview_path[48]{};
uint8_t files_preview_data[512]{};
uint32_t files_preview_size = 0;
uint32_t files_preview_total = 0;
uint32_t files_preview_checksum = 0;

constexpr const char* files_place_names[5] = {"ROOT", "APPS", "CONFIG", "SECURITY", "SYSTEM"};
constexpr const char* files_place_paths[5] = {"/", "/apps", "/config", "/security", "/system"};

void files_uint_text(uint32_t value, char output[16]) {
    char reverse[11];
    uint32_t count = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    uint32_t index = 0;
    while (count) output[index++] = reverse[--count];
    output[index] = 0;
}

void files_truncate(const char* input, char* output, uint32_t capacity, uint32_t maximum) {
    if (!output || capacity == 0U) return;
    output[0] = 0;
    if (!input) return;
    uint32_t length = 0;
    while (input[length] && length < maximum && length + 1U < capacity) {
        output[length] = input[length];
        ++length;
    }
    if (input[length] && length + 4U < capacity && length >= 3U) {
        output[length++] = '.';
        output[length++] = '.';
        output[length++] = '.';
    }
    output[length] = 0;
}

void files_ensure_visible() {
    if (files_entry_count == 0U) { files_focus = 0; files_scroll = 0; return; }
    if (files_focus >= files_entry_count) files_focus = files_entry_count - 1U;
    if (files_focus < files_scroll) files_scroll = files_focus;
    if (files_focus >= files_scroll + files_visible_rows) files_scroll = files_focus - files_visible_rows + 1U;
}

void files_reload() {
    files_entry_count = storage::list_directory(files_path, files_entries, files_capacity);
    files_ensure_visible();
}

bool files_set_path(const char* path) {
    char normalized[48];
    if (!storage::browser_directory_exists(path, normalized)) return false;
    string_copy(files_path, normalized, sizeof(files_path));
    files_focus = 0;
    files_scroll = 0;
    files_preview_open = false;
    files_reload();
    if (!files_navigation_reported && !string_equal(files_path, "/")) {
        serial::line("UI_FILES_NAV_OK");
        files_navigation_reported = true;
    }
    return true;
}

bool files_go_parent() {
    char parent[48];
    if (!storage::browser_parent(files_path, parent)) return false;
    return files_set_path(parent);
}

bool files_data_is_binary(const uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        const uint8_t value = data[i];
        if (value == 0U) return true;
        if (value < 32U && value != '\n' && value != '\r' && value != '\t') return true;
    }
    return false;
}

bool files_open_focused() {
    files_reload();
    if (files_entry_count == 0U || files_focus >= files_entry_count) return false;
    const storage::BrowserEntry& entry = files_entries[files_focus];
    if (entry.type == storage::zfs_type_directory) return files_set_path(entry.path);

    uint32_t copied = 0, total = 0;
    if (!storage::read_file_preview(entry.path, files_preview_data, sizeof(files_preview_data), copied, total)) return false;
    string_copy(files_preview_path, entry.path, sizeof(files_preview_path));
    files_preview_size = copied;
    files_preview_total = total;
    files_preview_checksum = entry.checksum;
    files_preview_binary = files_data_is_binary(files_preview_data, files_preview_size);
    files_preview_open = true;
    if (!files_preview_reported) {
        serial::line("UI_FILES_PREVIEW_OK");
        files_preview_reported = true;
    }
    return true;
}

void files_close_preview() { files_preview_open = false; }

uint8_t files_active_place() {
    for (uint8_t i = 0; i < 5U; ++i) if (string_equal(files_path, files_place_paths[i])) return i;
    return 0xFFU;
}

void draw_files_place(uint8_t index, int32_t y) {
    const bool selected = files_active_place() == index;
    const bool hovered = files_hover_place == index;
    if (selected) fill_round_rect(window_x + files_sidebar_x_offset, y, files_sidebar_width, 30, 8, current_theme.accent_soft);
    else if (hovered) fill_round_rect(window_x + files_sidebar_x_offset, y, files_sidebar_width, 30, 8,
                                      mix(current_theme.surface_high, current_theme.accent, 1U, 6U));
    draw_text(window_x + 28, y + 11, files_place_names[index], selected ? current_theme.text : current_theme.text_muted, 1);
}

void draw_files_entry_icon(int32_t x, int32_t y, bool directory) {
    const uint32_t accent = directory ? rgb(74,156,231) : rgb(139,124,246);
    fill_round_rect(x, y, 24, 20, 5, mix(current_theme.surface_high, accent, 2U, 5U));
    if (directory) {
        fill_round_rect(x + 4, y + 7, 16, 10, 2, accent);
        fill_round_rect(x + 5, y + 4, 8, 6, 2, accent);
    } else {
        draw_char(x + 9, y + 7, 'F', accent, 1);
    }
}

void render_files_list() {
    files_reload();
    draw_window_chrome("ZEN FILES", "ZENOVFS1");
    draw_text(window_x + 16, window_y + 68, "PLACES", current_theme.text_muted, 1);
    for (uint8_t i = 0; i < 5U; ++i) draw_files_place(i, window_y + 88 + static_cast<int32_t>(i * 38U));

    fill_rect(window_x + 128, window_y + 56, 1, 312, current_theme.border);
    const int32_t content_x = window_x + files_content_x_offset;
    fill_round_rect(content_x, window_y + 60, files_content_width, 36, 8, current_theme.surface_low);
    fill_round_rect(content_x + 6, window_y + 66, 24, 24, 7,
                    string_equal(files_path, "/") ? current_theme.surface_high : current_theme.accent_soft);
    draw_char(content_x + 15, window_y + 75, '<', current_theme.text, 1);
    char path_label[45];
    files_truncate(files_path, path_label, sizeof(path_label), 43U);
    draw_text(content_x + 40, window_y + 75, path_label, current_theme.text, 1);

    draw_text(content_x + 8, window_y + 108, "NAME", current_theme.text_muted, 1);
    draw_text(content_x + 230, window_y + 108, "TYPE", current_theme.text_muted, 1);
    draw_text_right(content_x + files_content_width - 8, window_y + 108, "SIZE", current_theme.text_muted, 1);
    fill_rect(content_x, window_y + 120, files_content_width, 1, current_theme.border);

    for (uint32_t visible = 0; visible < files_visible_rows; ++visible) {
        const uint32_t index = files_scroll + visible;
        if (index >= files_entry_count) break;
        const storage::BrowserEntry& entry = files_entries[index];
        const int32_t row_y = window_y + files_row_y_offset + static_cast<int32_t>(visible * files_row_height);
        const bool focused = index == files_focus;
        const bool hovered = static_cast<int32_t>(index) == files_hover_row;
        if (focused) fill_round_rect(content_x, row_y, files_content_width, files_row_height - 2, 7, current_theme.accent_soft);
        else if (hovered) fill_round_rect(content_x, row_y, files_content_width, files_row_height - 2, 7,
                                          mix(current_theme.surface_high, current_theme.accent, 1U, 7U));
        const bool directory = entry.type == storage::zfs_type_directory;
        draw_files_entry_icon(content_x + 6, row_y + 4, directory);
        char name[30];
        files_truncate(entry.name, name, sizeof(name), 26U);
        draw_text(content_x + 38, row_y + 11, name, current_theme.text, 1);
        draw_text(content_x + 230, row_y + 11, directory ? "DIR" : "FILE", current_theme.text_muted, 1);
        if (!directory) {
            char size[16];
            files_uint_text(entry.size, size);
            draw_text_right(content_x + files_content_width - 14, row_y + 11, size, current_theme.text_muted, 1);
        }
    }

    fill_rect(content_x, window_y + 342, files_content_width, 1, current_theme.border);
    draw_text(content_x + 8, window_y + 354, "UP/DOWN SELECT  ENTER OPEN  BACKSPACE UP", current_theme.text_muted, 1);
    draw_text(content_x + 8, window_y + 374, "GEN", current_theme.text_muted, 1);
    draw_uint(content_x + 32, window_y + 374, storage::browser_generation(), current_theme.text);
    draw_text_right(content_x + files_content_width - 8, window_y + 374,
                    files_entry_count == files_capacity ? "32+ ITEMS" : "LIVE INDEX", current_theme.success, 1);
}

char files_hex_digit(uint8_t value) { return value < 10U ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10U); }

void render_files_binary_preview(int32_t x, int32_t y) {
    draw_text(x, y, "BINARY PREVIEW", current_theme.warning, 1);
    uint32_t offset = 0;
    for (uint32_t row = 0; row < 8U && offset < files_preview_size; ++row) {
        char line[49];
        uint32_t at = 0;
        for (uint32_t column = 0; column < 16U && offset < files_preview_size; ++column, ++offset) {
            const uint8_t value = files_preview_data[offset];
            line[at++] = files_hex_digit(static_cast<uint8_t>(value >> 4U));
            line[at++] = files_hex_digit(static_cast<uint8_t>(value & 0x0FU));
            line[at++] = ' ';
        }
        line[at] = 0;
        draw_text(x, y + 22 + static_cast<int32_t>(row * 18U), line, current_theme.text, 1);
    }
}

void render_files_text_preview(int32_t x, int32_t y) {
    uint32_t position = 0;
    for (uint32_t row = 0; row < 19U && position < files_preview_size; ++row) {
        char line[73];
        uint32_t length = 0;
        while (position < files_preview_size && length < 72U) {
            uint8_t value = files_preview_data[position++];
            if (value == '\r') continue;
            if (value == '\n') break;
            if (value == '\t') value = ' ';
            line[length++] = value >= 32U && value <= 126U ? static_cast<char>(value) : '?';
        }
        line[length] = 0;
        draw_text(x, y + static_cast<int32_t>(row * 11U), line, current_theme.text, 1);
    }
}

void render_files_preview() {
    draw_window_chrome("ZEN FILES", "PREVIEW");
    char name[60];
    files_truncate(files_preview_path, name, sizeof(name), 56U);
    draw_text(window_x + 24, window_y + 68, name, current_theme.text, 1);
    draw_text(window_x + 24, window_y + 88, files_preview_binary ? "BINARY" : "TEXT", current_theme.text_muted, 1);
    draw_text(window_x + 120, window_y + 88, "SIZE", current_theme.text_muted, 1);
    draw_uint(window_x + 150, window_y + 88, files_preview_total, current_theme.text);
    draw_text(window_x + 228, window_y + 88, "CHECKSUM", current_theme.text_muted, 1);
    draw_uint(window_x + 288, window_y + 88, files_preview_checksum, current_theme.text);
    fill_round_rect(window_x + 16, window_y + 112, 480, 230, 8, current_theme.terminal);
    if (files_preview_binary) render_files_binary_preview(window_x + 28, window_y + 128);
    else render_files_text_preview(window_x + 28, window_y + 128);
    fill_rect(window_x + 16, window_y + 354, 480, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 366, "BACKSPACE / ESC  RETURN", current_theme.text_muted, 1);
    if (files_preview_total > files_preview_size)
        draw_text_right(window_x + 488, window_y + 366, "FIRST 512 BYTES", current_theme.warning, 1);
}

void render_files() {
    if (files_preview_open) render_files_preview();
    else render_files_list();
}

void render_scene() {
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    const int32_t saved_window_y = window_y;
    if (active_view != View::launcher) window_y += ui_scene_offset_y;
    if (active_view == View::terminal) render_terminal();
    else if (active_view == View::files) render_files();
    else if (active_view == View::settings) render_settings();
    else render_launcher();
    window_y = saved_window_y;
    draw_taskbar();
    draw_quick_settings();
    draw_status_center();
}
