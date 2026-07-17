char terminal_cell_char(uint32_t row, uint32_t column) {
    if (row >= console::height || column >= console::width) return 0;
    return static_cast<char>(console::vga[row * console::width + column] & 0xFFU);
}

bool terminal_home_visible() {
    return terminal_cell_char(4, 4) == 'S' &&
           terminal_cell_char(4, 5) == 'Y' &&
           terminal_cell_char(13, 4) == 'W';
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
        const int32_t y = origin_y + static_cast<int32_t>(visual_row * 8U);
        if (background != vga_colors[0]) fill_rect(x, y, 6, 8, background);
        if (value >= 32 && value <= 126) draw_char(x, y, value, foreground, 1);
    }
}

void draw_console_span(uint32_t source_row, uint32_t start_column, uint32_t count, int32_t x, int32_t y) {
    if (source_row >= console::height || start_column >= console::width) return;
    const uint32_t available = console::width - start_column;
    if (count > available) count = available;
    for (uint32_t i = 0; i < count; ++i) {
        const uint16_t cell = console::vga[source_row * console::width + start_column + i];
        const char value = static_cast<char>(cell & 0xFFU);
        const uint8_t attribute = static_cast<uint8_t>(cell >> 8U);
        if (value == '|') break;
        if (value >= 32 && value <= 126) draw_char(x + static_cast<int32_t>(i * 6U), y, value, vga_colors[attribute & 0x0FU], 1);
    }
}

void draw_terminal_caret(int32_t origin_x, int32_t origin_y, uint32_t first_row) {
    if (console::row < first_row || console::row >= console::content_bottom) return;
    const int32_t caret_x = origin_x + static_cast<int32_t>(console::column * 6U);
    const int32_t caret_y = origin_y + static_cast<int32_t>((console::row - first_row) * 8U);
    if (string_equal(terminal_cursor, "block")) fill_rect_alpha(caret_x, caret_y, 5, 7, current_theme.accent, 90);
    else if (string_equal(terminal_cursor, "underline")) fill_rect_alpha(caret_x, caret_y + 7, 5, 1, current_theme.accent, 220);
    else fill_rect_alpha(caret_x, caret_y, 1, 7, current_theme.accent, 230);
}

void render_terminal_home() {
    const int32_t panel_x = window_x + 8;
    const int32_t panel_y = window_y + 52;
    const int32_t label_x = window_x + 24;
    const int32_t value_x = window_x + 152;
    fill_round_rect(panel_x, panel_y, 496, 224, 8, current_theme.terminal);

    draw_text(window_x + 24, window_y + 72, "SYSTEM READY", current_theme.text, 2);
    draw_text(window_x + 24, window_y + 94, "ZENOVOS 0.1.1 / SIGNED POLICY", current_theme.text_muted, 1);

    draw_text(label_x, window_y + 122, "ARCHITECTURE", current_theme.text_muted, 1);
    draw_console_span(6, 16, 24, value_x, window_y + 122);
    draw_text(label_x, window_y + 144, "MEMORY", current_theme.text_muted, 1);
    draw_console_span(7, 16, 28, value_x, window_y + 144);
    draw_text(label_x, window_y + 166, "STORAGE", current_theme.text_muted, 1);
    draw_console_span(8, 16, 32, value_x, window_y + 166);
    draw_text(label_x, window_y + 188, "APPLICATIONS", current_theme.text_muted, 1);
    draw_console_span(9, 16, 24, value_x, window_y + 188);
    draw_text(label_x, window_y + 210, "PMM FREE", current_theme.text_muted, 1);
    draw_uint(value_x, window_y + 210, pmm::free_bytes() >> 10U, current_theme.text);
    draw_text(value_x + 60, window_y + 210, "KIB", current_theme.text_muted, 1);
    draw_text(label_x, window_y + 232, "SECURITY", current_theme.text_muted, 1);
    draw_text(value_x, window_y + 232, "SIGNED ZGDB / VERIFIED", current_theme.success, 1);

    fill_rect(window_x + 24, window_y + 246, window_width - 48, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 256, "F1 HELP  F2 SYSTEM  F3 FILES  F4 HOME", current_theme.text_muted, 1);
    draw_console_content_row(console::row, 0, window_x + 16, window_y + 270);
    draw_terminal_caret(window_x + 16, window_y + 270, console::row);
}

void render_terminal_cells() {
    const int32_t panel_x = window_x + 8;
    const int32_t panel_y = window_y + 52;
    fill_round_rect(panel_x, panel_y, 496, 224, 8, current_theme.terminal);

    if (terminal_home_visible()) {
        render_terminal_home();
    } else {
        const int32_t origin_x = window_x + 16;
        const int32_t origin_y = window_y + 72;
        for (uint32_t row = console::content_top; row < console::content_bottom; ++row)
            draw_console_content_row(row, row - console::content_top, origin_x, origin_y);
        draw_terminal_caret(origin_x, origin_y, console::content_top);
    }

    fill_rect(window_x + 16, window_y + 296, window_width - 32, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 316, "SHELL", current_theme.text, 1);
    draw_text_right(window_x + window_width - 20, window_y + 316, "80 X 25", current_theme.text_muted, 1);
}

void render_terminal_placeholder() {
    const int32_t panel_x = window_x + 8;
    const int32_t panel_y = window_y + 52;
    fill_round_rect(panel_x, panel_y, 496, 224, 8, current_theme.terminal);
    draw_text(window_x + 24, window_y + 88, "ZENOV TERMINAL", current_theme.text, 2);
    draw_text(window_x + 24, window_y + 116, "INITIALIZING SHELL MIRROR", current_theme.text_muted, 1);
    fill_rect(window_x + 24, window_y + 148, 64, 2, current_theme.accent);
    fill_rect(window_x + 16, window_y + 296, window_width - 32, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 316, "PS2", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 20, window_y + 316, "STARTING", current_theme.success, 1);
}

void render_terminal() {
    draw_window_chrome("ZEN TERMINAL", "I686 / LOCAL");
    if (terminal_sync_started) render_terminal_cells(); else render_terminal_placeholder();
}

void draw_file_card(int32_t x, int32_t y, const char* name, const char* detail, uint32_t accent) {
    fill_round_rect(x, y, 168, 96, 10, current_theme.surface_high);
    fill_round_rect(x + 16, y + 16, 36, 32, 8, accent);
    fill_rect_alpha(x + 21, y + 12, 20, 8, rgb(255,255,255), 60);
    draw_text(x + 16, y + 62, name, current_theme.text, 1);
    draw_text(x + 16, y + 78, detail, current_theme.text_muted, 1);
}

void render_files() {
    draw_window_chrome("ZEN FILES", "/DATA");
    draw_text(window_x + 16, window_y + 68, "PLACES", current_theme.text_muted, 1);
    fill_round_rect(window_x + 12, window_y + 88, 112, 32, 8, current_theme.accent_soft);
    draw_text(window_x + 28, window_y + 100, "HOME", current_theme.text, 1);
    draw_text(window_x + 28, window_y + 144, "APPS", current_theme.text_muted, 1);
    draw_text(window_x + 28, window_y + 184, "SYSTEM", current_theme.text_muted, 1);
    draw_text(window_x + 28, window_y + 224, "CONFIG", current_theme.text_muted, 1);

    fill_rect(window_x + 128, window_y + 56, 1, 312, current_theme.border);
    draw_text(window_x + 144, window_y + 68, "WORKSPACE", current_theme.text, 2);
    draw_text(window_x + 144, window_y + 92, "ZENOVFS1 / READ-WRITE", current_theme.text_muted, 1);
    draw_file_card(window_x + 144, window_y + 120, "SYSTEM", "CORE FILES", rgb(74,156,231));
    draw_file_card(window_x + 328, window_y + 120, "APPS", "7 PACKAGES", rgb(139,124,246));
    draw_file_card(window_x + 144, window_y + 232, "DOCS", "REFERENCE", rgb(102,202,161));
    draw_file_card(window_x + 328, window_y + 232, "PROJECTS", "WORKSPACE", rgb(238,163,82));
}

void render_settings() {
    draw_window_chrome("SETTINGS", "APPEARANCE");
    draw_text(window_x + 16, window_y + 68, "COLOR THEME", current_theme.text_muted, 1);
    const uint32_t themes[3] = {rgb(139,124,246), rgb(126,142,177), rgb(245,174,82)};
    const char* names[3] = {"MIDNIGHT", "GRAPHITE", "AMBER"};
    for (uint32_t i = 0; i < 3U; ++i) {
        const int32_t x = window_x + 16 + static_cast<int32_t>(i * 160U);
        const bool selected = runtime_theme == i;
        fill_round_rect(x, window_y + 92, 144, 80, 10, selected ? current_theme.accent_soft : current_theme.surface_high);
        fill_circle_alpha(x + 24, window_y + 116, 9, themes[i], 255);
        draw_text(x + 44, window_y + 112, names[i], current_theme.text, 1);
        draw_text(x + 16, window_y + 148, selected ? "ACTIVE" : "SELECT", selected ? current_theme.success : current_theme.text_muted, 1);
    }

    fill_rect(window_x + 16, window_y + 204, 480, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 222, "MOTION", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 24, window_y + 222, desktop_animations ? "ENABLED" : "REDUCED", current_theme.text, 1);
    fill_rect(window_x + 16, window_y + 248, 480, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 266, "TERMINAL", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 24, window_y + 266, "80 X 25 / PS2", current_theme.text, 1);
    fill_rect(window_x + 16, window_y + 292, 480, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 310, "CONFIG", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 24, window_y + 310, "/DATA/CONFIG/UI.CFG", current_theme.accent, 1);
}

void draw_launcher_card(int32_t x, int32_t y, ShellIcon icon, const char* name, uint32_t accent) {
    fill_round_rect(x, y, 144, 128, 12, current_theme.surface_high);
    fill_round_rect(x + 48, y + 20, 48, 48, 12, accent);
    draw_shell_icon(icon, x + 61, y + 33, rgb(255,255,255));
    draw_text_center(x + 72, y + 88, name, current_theme.text, 1);
}

void render_launcher() {
    draw_window_chrome("APPLICATIONS", "3 SURFACES");
    draw_text(window_x + 16, window_y + 76, "SELECT A SURFACE", current_theme.text_muted, 1);
    draw_launcher_card(window_x + 16, window_y + 120, ShellIcon::terminal, "TERMINAL", rgb(139,124,246));
    draw_launcher_card(window_x + 184, window_y + 120, ShellIcon::files, "FILES", rgb(74,156,231));
    draw_launcher_card(window_x + 352, window_y + 120, ShellIcon::settings, "SETTINGS", rgb(102,202,161));
}

void render_scene() {
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    if (active_view == View::terminal) render_terminal();
    else if (active_view == View::files) render_files();
    else if (active_view == View::settings) render_settings();
    else render_launcher();
    draw_dock();
}
