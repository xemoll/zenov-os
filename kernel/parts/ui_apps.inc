constexpr int32_t terminal_panel_x_offset = 8;
constexpr int32_t terminal_panel_y_offset = 52;
constexpr int32_t terminal_panel_width = 496;
constexpr int32_t terminal_panel_height = 336;
constexpr int32_t terminal_line_height = 10;

constexpr int32_t launcher_row_x_offset = 48;
constexpr int32_t launcher_row_width = 416;
constexpr int32_t launcher_row_height = 62;
constexpr int32_t launcher_row_y_offsets[3] = {88, 160, 232};

constexpr int32_t settings_theme_width = 136;
constexpr int32_t settings_theme_height = 58;
constexpr int32_t settings_theme_x_offsets[3] = {32, 188, 344};
constexpr int32_t settings_theme_y_offset = 88;

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

void render_terminal_cells() {
    const int32_t panel_x = window_x + terminal_panel_x_offset;
    const int32_t panel_y = window_y + terminal_panel_y_offset;
    fill_round_rect(panel_x, panel_y, terminal_panel_width, terminal_panel_height, 7, current_theme.terminal);
    const int32_t origin_x = window_x + 16;
    const int32_t origin_y = window_y + 68;
    for (uint32_t row = console::content_top; row < console::content_bottom; ++row)
        draw_console_content_row(row, row - console::content_top, origin_x, origin_y);
    draw_terminal_caret(origin_x, origin_y, console::content_top);
}

void render_terminal_placeholder() {
    fill_round_rect(window_x + terminal_panel_x_offset, window_y + terminal_panel_y_offset,
                    terminal_panel_width, terminal_panel_height, 7, current_theme.terminal);
    draw_text(window_x + 24, window_y + 88, "TERMINAL", current_theme.text, 2);
    draw_text(window_x + 24, window_y + 116, "STARTING LOCAL SESSION", current_theme.text_muted, 1);
}

void render_terminal() {
    draw_window_chrome("TERMINAL", "LOCAL");
    if (terminal_sync_started) render_terminal_cells(); else render_terminal_placeholder();
}

void draw_settings_theme(uint32_t index, uint32_t color, const char* name, HoverTarget target) {
    const int32_t x = window_x + settings_theme_x_offsets[index];
    const int32_t y = window_y + settings_theme_y_offset;
    const bool selected = runtime_theme == index;
    const bool focused = settings_focus == index;
    uint32_t fill = current_theme.surface_high;
    if (selected) fill = current_theme.accent_soft;
    else if (focused || is_hovered(target)) fill = mix(current_theme.surface_high, color, 1U, 7U);
    fill_round_rect(x, y, settings_theme_width, settings_theme_height, 8, fill);
    fill_circle_alpha(x + 22, y + 22, 7, color, 255);
    draw_text(x + 40, y + 18, name, current_theme.text, 1);
    draw_text(x + 16, y + 40, selected ? "ACTIVE" : "SELECT", selected ? current_theme.success : current_theme.text_muted, 1);
    if (focused) fill_rect(x + 16, y + settings_theme_height - 3, settings_theme_width - 32, 2, current_theme.accent);
}

void draw_settings_row(int32_t y, const char* label, const char* value, uint32_t value_color) {
    draw_text(window_x + 24, y, label, current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 24, y, value, value_color, 1);
    fill_rect(window_x + 20, y + 22, window_width - 40, 1, current_theme.border);
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

void render_settings() {
    draw_window_chrome("SETTINGS", "DESKTOP");
    draw_text(window_x + 24, window_y + 68, "THEME", current_theme.text_muted, 1);
    const uint32_t themes[3] = {rgb(124,137,232), rgb(126,142,177), rgb(216,151,83)};
    const char* names[3] = {"MIDNIGHT", "GRAPHITE", "AMBER"};
    const HoverTarget targets[3] = {HoverTarget::theme_midnight, HoverTarget::theme_graphite, HoverTarget::theme_amber};
    for (uint32_t i = 0; i < 3U; ++i) draw_settings_theme(i, themes[i], names[i], targets[i]);

    draw_text(window_x + 24, window_y + 172, "DISPLAY", current_theme.text_muted, 1);
    draw_display_value(window_x + window_width - 24, window_y + 172);
    fill_rect(window_x + 20, window_y + 194, window_width - 40, 1, current_theme.border);
    draw_settings_row(window_y + 212, "MOTION", desktop_animations ? "ON" : "REDUCED", current_theme.text);
    draw_settings_row(window_y + 256, "CURSOR", terminal_cursor, current_theme.text);
    draw_settings_row(window_y + 300, "CONFIG", "/DATA/CONFIG/UI.CFG", current_theme.accent);
    draw_text(window_x + 24, window_y + 354, "LEFT RIGHT ENTER   F9 DISPLAY", current_theme.text_muted, 1);
}

void draw_launcher_row(uint32_t index, ShellIcon icon, const char* name, const char* detail,
                       const char* shortcut, uint32_t accent, HoverTarget target) {
    const int32_t x = window_x + launcher_row_x_offset;
    const int32_t y = window_y + launcher_row_y_offsets[index];
    const bool focused = launcher_focus == index;
    uint32_t fill = current_theme.surface_high;
    if (focused || is_hovered(target)) fill = mix(current_theme.surface_high, accent, 1U, 8U);
    fill_round_rect(x, y, launcher_row_width, launcher_row_height, 9, fill);
    fill_round_rect(x + 12, y + 11, 40, 40, 9, current_theme.surface_low);
    draw_shell_icon(icon, x + 21, y + 20, accent);
    draw_text(x + 68, y + 17, name, current_theme.text, 1);
    draw_text(x + 68, y + 36, detail, current_theme.text_muted, 1);
    draw_text_right(x + launcher_row_width - 18, y + 27, shortcut, current_theme.text_muted, 1);
    if (focused) fill_rect(x + 68, y + launcher_row_height - 5, 72, 2, current_theme.accent);
}

void render_launcher() {
    draw_window_chrome("APPLICATIONS", "LOCAL");
    draw_launcher_row(0, ShellIcon::terminal, "TERMINAL", "SHELL AND SYSTEM TOOLS", "F5", current_theme.accent, HoverTarget::launcher_terminal);
    draw_launcher_row(1, ShellIcon::files, "FILES", "ZENOVFS BROWSER", "F6", rgb(91,146,199), HoverTarget::launcher_files);
    draw_launcher_row(2, ShellIcon::settings, "SETTINGS", "DISPLAY AND THEME", "F7", rgb(105,181,145), HoverTarget::launcher_settings);
    draw_text(window_x + 48, window_y + 326, "ARROWS SELECT   ENTER OPEN   ESC CLOSE", current_theme.text_muted, 1);
}
