constexpr int32_t terminal_panel_x_offset = 8;
constexpr int32_t terminal_panel_y_offset = 52;
constexpr int32_t terminal_panel_width = 496;
constexpr int32_t terminal_panel_height = 336;
constexpr int32_t terminal_line_height = 10;

constexpr int32_t launcher_row_x_offset = 40;
constexpr int32_t launcher_row_width = 432;
constexpr int32_t launcher_row_height = 62;
constexpr int32_t launcher_row_y_offsets[3] = {102, 174, 246};

constexpr int32_t settings_theme_width = 136;
constexpr int32_t settings_theme_height = 58;
constexpr int32_t settings_theme_x_offsets[3] = {32, 188, 344};
constexpr int32_t settings_theme_y_offset = 88;
constexpr int32_t settings_display_y_offset = 170;
constexpr int32_t settings_display_height = 54;
constexpr int32_t settings_motion_y_offset = 244;
constexpr int32_t settings_cursor_y_offset = 288;

char terminal_cell_char(uint32_t row, uint32_t column) {
    if (row >= console::height || column >= console::width) return 0;
    return static_cast<char>(console::vga[row * console::width + column] & 0xFFU);
}

bool terminal_home_visible() {
    return terminal_cell_char(4, 4) == 'S' && terminal_cell_char(4, 5) == 'Y' &&
           terminal_cell_char(13, 4) == 'W' && terminal_cell_char(13, 5) == 'O';
}

void draw_console_content_row(uint32_t source_row, uint32_t visual_row, int32_t origin_x, int32_t origin_y) {
    if (source_row >= console::height) return;
    for (uint32_t column = 0; column < console::width; ++column) {
        const uint16_t cell = console::vga[source_row * console::width + column];
        const char value = static_cast<char>(cell & 0xFFU);
        const uint8_t attribute = static_cast<uint8_t>(cell >> 8U);
        const uint32_t foreground = vga_colors[attribute & 0x0FU];
        const uint32_t background = vga_colors[(attribute >> 4U) & 0x07U];
        const int32_t x = origin_x + static_cast<int32_t>(column * 6U);
        const int32_t y = origin_y + static_cast<int32_t>(visual_row * terminal_line_height);
        if (background != vga_colors[0]) fill_rect(x, y, 6, terminal_line_height, background);
        if (value >= 32 && value <= 126) draw_char(x, y, value, foreground, 1);
    }
}

void draw_terminal_caret(int32_t origin_x, int32_t origin_y, uint32_t first_row) {
    if (console::row < first_row || console::row >= console::content_bottom) return;
    const int32_t caret_x = origin_x + static_cast<int32_t>(console::column * 6U);
    const int32_t caret_y = origin_y + static_cast<int32_t>((console::row - first_row) * terminal_line_height);
    if (string_equal(terminal_cursor, "block")) fill_rect_alpha(caret_x, caret_y, 5, 7, current_theme.accent, 90);
    else if (string_equal(terminal_cursor, "underline")) fill_rect_alpha(caret_x, caret_y + 7, 5, 1, current_theme.accent, 220);
    else fill_rect_alpha(caret_x, caret_y, 1, 7, current_theme.accent, 230);
}

void draw_terminal_badge(int32_t x, int32_t y, const char* label, uint32_t color) {
    const int32_t badge_width = text_width(label) + 22;
    fill_round_rect(x, y, badge_width, 22, 11, current_theme.surface_high);
    fill_circle_alpha(x + 10, y + 11, 3, color, 255);
    draw_text(x + 18, y + 8, label, current_theme.text_muted, 1);
}

void render_terminal_home() {
    const int32_t panel_x = window_x + terminal_panel_x_offset;
    const int32_t panel_y = window_y + terminal_panel_y_offset;
    fill_round_rect(panel_x, panel_y, terminal_panel_width, terminal_panel_height, 10, current_theme.terminal);
    fill_circle_alpha(window_x + 430, window_y + 92, 92, current_theme.accent, 7);
    draw_text(window_x + 24, window_y + 80, "ZENOVOS", current_theme.text, 2);
    draw_text(window_x + 24, window_y + 106, "0.1.1 NATIVE DESKTOP", current_theme.text_muted, 1);
    draw_terminal_badge(window_x + 24, window_y + 128, "READY", current_theme.success);
    draw_terminal_badge(window_x + 104, window_y + 128, "LOCAL", current_theme.accent);
    draw_terminal_badge(window_x + 184, window_y + 128, "I686", current_theme.warning);
    fill_rect(window_x + 24, window_y + 166, 448, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 184, "HELP  COMMANDS     F6  FILES     F7  SETTINGS", current_theme.text_muted, 1);
    draw_text(window_x + 24, window_y + 204, "F8  APPLICATIONS   F9  NEXT DISPLAY MODE", current_theme.text_muted, 1);
    fill_round_rect(window_x + 16, window_y + 232, 480, 52, 8, current_theme.surface_low);
    draw_text(window_x + 28, window_y + 244, "SHELL", current_theme.text_muted, 1);
    draw_console_content_row(console::row, 0, window_x + 28, window_y + 264);
    draw_terminal_caret(window_x + 28, window_y + 264, console::row);
    draw_text(window_x + 24, window_y + 314, "TAB COMPLETES  /  ARROWS HISTORY  /  ESC CLEARS", current_theme.text_muted, 1);
}

void render_terminal_cells() {
    if (terminal_home_visible()) {
        render_terminal_home();
        return;
    }
    const int32_t panel_x = window_x + terminal_panel_x_offset;
    const int32_t panel_y = window_y + terminal_panel_y_offset;
    fill_round_rect(panel_x, panel_y, terminal_panel_width, terminal_panel_height, 10, current_theme.terminal);
    const int32_t origin_x = window_x + 16;
    const int32_t origin_y = window_y + 68;
    for (uint32_t row = console::content_top; row < console::content_bottom; ++row)
        draw_console_content_row(row, row - console::content_top, origin_x, origin_y);
    draw_terminal_caret(origin_x, origin_y, console::content_top);
}

void render_terminal_placeholder() {
    fill_round_rect(window_x + terminal_panel_x_offset, window_y + terminal_panel_y_offset,
                    terminal_panel_width, terminal_panel_height, 10, current_theme.terminal);
    draw_text(window_x + 24, window_y + 88, "TERMINAL", current_theme.text, 2);
    draw_text(window_x + 24, window_y + 116, "STARTING LOCAL SESSION", current_theme.text_muted, 1);
}

void render_terminal() {
    draw_window_chrome("TERMINAL", "LOCAL SHELL");
    if (terminal_sync_started) render_terminal_cells(); else render_terminal_placeholder();
}

void draw_settings_theme(uint32_t index, uint32_t color, const char* name, HoverTarget target) {
    const int32_t x = window_x + settings_theme_x_offsets[index];
    const int32_t y = window_y + settings_theme_y_offset;
    const bool selected = runtime_theme == index;
    const bool focused = settings_focus == index;
    uint32_t fill = current_theme.surface_high;
    if (selected) fill = current_theme.accent_soft;
    else if (focused || is_hovered(target)) fill = mix(current_theme.surface_high, color, 1U, 6U);
    fill_round_rect(x, y, settings_theme_width, settings_theme_height, 10, fill);
    fill_circle_alpha(x + 22, y + 22, 7, color, 255);
    draw_text(x + 40, y + 18, name, current_theme.text, 1);
    draw_text(x + 16, y + 40, selected ? "ACTIVE" : "SELECT", selected ? current_theme.success : current_theme.text_muted, 1);
    if (focused) draw_round_border(x, y, settings_theme_width, settings_theme_height, 10, current_theme.accent);
}

void draw_display_value(int32_t right, int32_t y) {
    const DisplayMode mode = display_modes[active_display_mode];
    char text[16]; uint32_t length = 0;
    char reverse[6]; uint32_t count = 0, value = mode.width;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
    while (count) text[length++] = reverse[--count];
    text[length++] = 'X'; count = 0; value = mode.height;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
    while (count) text[length++] = reverse[--count];
    text[length] = 0;
    draw_text_right(right, y, text, current_theme.text, 1);
}

void draw_settings_display() {
    const int32_t x = window_x + 20;
    const int32_t y = window_y + settings_display_y_offset;
    const int32_t card_width = window_width - 40;
    const bool focused = settings_focus == 3U;
    const bool hovered = is_hovered(HoverTarget::settings_display);
    fill_round_rect(x, y, card_width, settings_display_height, 10,
                    focused || hovered ? mix(current_theme.surface_high, current_theme.accent, 1U, 8U) : current_theme.surface_high);
    fill_round_rect(x + 12, y + 12, 30, 30, 8, current_theme.surface_low);
    draw_char(x + 23, y + 23, '<', current_theme.text_muted, 1);
    draw_text(x + 58, y + 14, "DISPLAY MODE", current_theme.text, 1);
    draw_text(x + 58, y + 33, "LEFT / RIGHT OR F9", current_theme.text_muted, 1);
    draw_display_value(x + card_width - 54, y + 23);
    fill_round_rect(x + card_width - 42, y + 12, 30, 30, 8, current_theme.surface_low);
    draw_char(x + card_width - 31, y + 23, '>', current_theme.text_muted, 1);
    if (focused) draw_round_border(x, y, card_width, settings_display_height, 10, current_theme.accent);
}

void draw_settings_row(int32_t y, const char* label, const char* value, uint32_t value_color,
                       uint8_t focus_index, HoverTarget target) {
    const int32_t x = window_x + 20;
    const int32_t row_width = window_width - 40;
    const bool focused = settings_focus == focus_index;
    const bool hovered = is_hovered(target);
    if (focused || hovered) fill_round_rect(x, y - 10, row_width, 34, 9, current_theme.surface_high);
    draw_text(window_x + 32, y, label, current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 32, y, value, value_color, 1);
    if (focused) fill_round_rect(window_x + 24, y + 20, row_width - 8, 2, 1, current_theme.accent);
}

void render_settings() {
    draw_window_chrome("SETTINGS", "APPEARANCE");
    draw_text(window_x + 24, window_y + 68, "THEME", current_theme.text_muted, 1);
    const uint32_t themes[3] = {rgb(125,140,255), rgb(132,148,188), rgb(222,155,84)};
    const char* names[3] = {"MIDNIGHT", "GRAPHITE", "AMBER"};
    const HoverTarget targets[3] = {HoverTarget::theme_midnight, HoverTarget::theme_graphite, HoverTarget::theme_amber};
    for (uint32_t i = 0; i < 3U; ++i) draw_settings_theme(i, themes[i], names[i], targets[i]);

    draw_settings_display();
    draw_settings_row(window_y + settings_motion_y_offset, "MOTION", desktop_animations ? "SMOOTH" : "REDUCED",
                      current_theme.text, 4U, HoverTarget::settings_motion);
    draw_settings_row(window_y + settings_cursor_y_offset, "TERMINAL CURSOR", terminal_cursor,
                      current_theme.text, 5U, HoverTarget::settings_cursor);
    draw_text(window_x + 24, window_y + 344, "CONFIG", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 24, window_y + 344, "/DATA/CONFIG/UI.CFG", current_theme.accent, 1);
    draw_text_center(window_x + window_width / 2, window_y + 374, "TAB SELECT   ARROWS CHANGE   ENTER APPLY", current_theme.text_muted, 1);
}

void draw_launcher_row(uint32_t index, ShellIcon icon, const char* name, const char* detail,
                       const char* shortcut, uint32_t accent, HoverTarget target) {
    const int32_t x = window_x + launcher_row_x_offset;
    const int32_t y = window_y + launcher_row_y_offsets[index];
    const bool focused = launcher_focus == index;
    uint32_t fill = current_theme.surface_high;
    if (focused || is_hovered(target)) fill = mix(current_theme.surface_high, accent, 1U, 7U);
    fill_round_rect(x, y, launcher_row_width, launcher_row_height, 11, fill);
    fill_round_rect(x + 12, y + 11, 40, 40, 10, current_theme.surface_low);
    draw_shell_icon(icon, x + 21, y + 20, accent);
    draw_text(x + 68, y + 17, name, current_theme.text, 1);
    draw_text(x + 68, y + 36, detail, current_theme.text_muted, 1);
    fill_round_rect(x + launcher_row_width - 56, y + 18, 38, 26, 8, current_theme.surface_low);
    draw_text_center(x + launcher_row_width - 37, y + 28, shortcut, current_theme.text_muted, 1);
    if (focused) fill_round_rect(x + 68, y + launcher_row_height - 5, 84, 2, 1, current_theme.accent);
}

void render_launcher() {
    draw_window_chrome("APPLICATIONS", "LOCAL SURFACES");
    draw_text(window_x + 40, window_y + 72, "OPEN A SYSTEM SURFACE", current_theme.text_muted, 1);
    draw_launcher_row(0, ShellIcon::terminal, "TERMINAL", "SHELL AND SYSTEM TOOLS", "F5", current_theme.accent, HoverTarget::launcher_terminal);
    draw_launcher_row(1, ShellIcon::files, "FILES", "ZENOVFS BROWSER", "F6", rgb(91,146,199), HoverTarget::launcher_files);
    draw_launcher_row(2, ShellIcon::settings, "SETTINGS", "DISPLAY AND THEME", "F7", rgb(105,181,145), HoverTarget::launcher_settings);
    draw_text_center(window_x + window_width / 2, window_y + 338, "ARROWS SELECT   ENTER OPEN   ESC CLOSE", current_theme.text_muted, 1);
}
