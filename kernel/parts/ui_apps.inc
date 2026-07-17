void render_terminal_cells() {
    const int32_t panel_x = window_x + 8;
    const int32_t panel_y = window_y + 52;
    const int32_t origin_x = window_x + 16;
    const int32_t origin_y = window_y + 64;
    fill_round_rect(panel_x, panel_y, 496, 224, 8, current_theme.terminal);
    for (uint32_t row = 0; row < console::height; ++row) {
        for (uint32_t column = 0; column < console::width; ++column) {
            const uint16_t cell = console::vga[row * console::width + column];
            const char value = static_cast<char>(cell & 0xFFU);
            const uint8_t attribute = static_cast<uint8_t>(cell >> 8U);
            const uint32_t foreground = vga_colors[attribute & 0x0FU];
            const uint32_t background = vga_colors[(attribute >> 4U) & 0x07U];
            const int32_t x = origin_x + static_cast<int32_t>(column * 6U);
            const int32_t y = origin_y + static_cast<int32_t>(row * 8U);
            if (background != vga_colors[0]) fill_rect(x, y, 6, 8, background);
            if (value >= 32 && value <= 126) draw_char(x, y, value, foreground, 1);
        }
    }
    const int32_t caret_x = origin_x + static_cast<int32_t>(console::column * 6U);
    const int32_t caret_y = origin_y + static_cast<int32_t>(console::row * 8U);
    if (string_equal(terminal_cursor, "block")) fill_rect_alpha(caret_x, caret_y, 5, 7, current_theme.accent, 90);
    else if (string_equal(terminal_cursor, "underline")) fill_rect_alpha(caret_x, caret_y + 7, 5, 1, current_theme.accent, 220);
    else fill_rect_alpha(caret_x, caret_y, 1, 7, current_theme.accent, 230);

    fill_rect(window_x + 16, window_y + 296, window_width - 32, 1, current_theme.border);
    fill_circle_alpha(window_x + 20, window_y + 320, 3, current_theme.success, 255);
    draw_text(window_x + 32, window_y + 316, "LIVE SHELL", current_theme.text, 1);
    draw_text_right(window_x + window_width - 20, window_y + 316, "TAB / HISTORY", current_theme.text_muted, 1);
}

void render_terminal_placeholder() {
    const int32_t panel_x = window_x + 8;
    const int32_t panel_y = window_y + 52;
    fill_round_rect(panel_x, panel_y, 496, 224, 8, current_theme.terminal);
    draw_text(window_x + 24, window_y + 80, "ZENOVOS GRAPHICAL TERMINAL", current_theme.text, 1);
    draw_text(window_x + 24, window_y + 104, "INITIALIZING SHELL MIRROR...", current_theme.text_muted, 1);
    draw_text(window_x + 24, window_y + 144, "ZENOV> ", current_theme.accent, 1);
    fill_rect(window_x + 84, window_y + 151, 5, 1, current_theme.accent);
    fill_rect(window_x + 16, window_y + 296, window_width - 32, 1, current_theme.border);
    draw_text(window_x + 24, window_y + 316, "PS2 INPUT", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 20, window_y + 316, "SHELL READY", current_theme.success, 1);
}

void render_terminal() {
    draw_window_chrome("ZEN TERMINAL", "LOCAL / I686");
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

void draw_toggle(int32_t x, int32_t y, bool enabled) {
    fill_round_rect(x, y, 40, 20, 10, enabled ? current_theme.accent : current_theme.surface_low);
    fill_circle_alpha(x + (enabled ? 30 : 10), y + 10, 7, current_theme.text, 255);
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

    fill_round_rect(window_x + 16, window_y + 196, 480, 56, 10, current_theme.surface_high);
    draw_text(window_x + 32, window_y + 216, "ANIMATIONS", current_theme.text, 1);
    draw_text(window_x + 32, window_y + 232, desktop_animations ? "ENABLED" : "REDUCED", current_theme.text_muted, 1);
    draw_toggle(window_x + 432, window_y + 214, desktop_animations);

    fill_round_rect(window_x + 16, window_y + 268, 480, 64, 10, current_theme.surface_high);
    draw_text(window_x + 32, window_y + 288, "TERMINAL", current_theme.text, 1);
    draw_text(window_x + 32, window_y + 306, "80 X 25 / PS2 / LIVE MIRROR", current_theme.text_muted, 1);
    draw_text_right(window_x + window_width - 32, window_y + 306, "UI.CFG", current_theme.accent, 1);
}

void draw_launcher_card(int32_t x, int32_t y, char letter, const char* name, uint32_t accent) {
    fill_round_rect(x, y, 224, 96, 10, current_theme.surface_high);
    fill_round_rect(x + 16, y + 16, 40, 40, 10, accent);
    draw_char(x + 31, y + 28, letter, current_theme.text, 2);
    draw_text(x + 72, y + 28, name, current_theme.text, 1);
    draw_text(x + 72, y + 48, "ZENOV SURFACE", current_theme.text_muted, 1);
}

void render_launcher() {
    draw_window_chrome("APPLICATIONS", "4 SURFACES");
    fill_round_rect(window_x + 16, window_y + 56, 480, 40, 10, current_theme.surface_low);
    draw_text(window_x + 32, window_y + 72, "SEARCH...", current_theme.text_muted, 1);
    draw_launcher_card(window_x + 16, window_y + 120, 'T', "TERMINAL", rgb(139,124,246));
    draw_launcher_card(window_x + 272, window_y + 120, 'F', "FILES", rgb(74,156,231));
    draw_launcher_card(window_x + 16, window_y + 232, 'S', "SETTINGS", rgb(102,202,161));
    draw_launcher_card(window_x + 272, window_y + 232, 'G', "GUARD", rgb(239,125,146));
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
