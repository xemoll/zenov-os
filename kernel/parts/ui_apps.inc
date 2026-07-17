void render_terminal_cells() {
    const int32_t origin_x = window_x + 15;
    const int32_t origin_y = window_y + 63;
    fill_round_rect(origin_x - 7, origin_y - 10, 494, 246, 10, current_theme.terminal);
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
    fill_round_rect(window_x + 15, window_y + 318, 480, 50, 10, current_theme.surface_low);
    fill_circle_alpha(window_x + 31, window_y + 343, 4, current_theme.success, 255);
    draw_text(window_x + 43, window_y + 339, "LIVE SHELL", current_theme.text, 1);
    draw_text(window_x + 145, window_y + 339, "TAB COMPLETE", current_theme.text_muted, 1);
    draw_text(window_x + 273, window_y + 339, "HISTORY", current_theme.text_muted, 1);
    draw_text(window_x + 385, window_y + 339, "SERIAL ON", current_theme.success, 1);
}

void render_terminal_placeholder() {
    const int32_t origin_x = window_x + 15, origin_y = window_y + 63;
    fill_round_rect(origin_x - 7, origin_y - 10, 494, 246, 10, current_theme.terminal);
    draw_text(origin_x + 12, origin_y + 15, "ZENOVOS GRAPHICAL TERMINAL", current_theme.text, 1);
    draw_text(origin_x + 12, origin_y + 39, "INITIALIZING LIVE VGA MIRROR...", current_theme.text_muted, 1);
    draw_text(origin_x + 12, origin_y + 71, "ZENOV> ", current_theme.accent, 1);
    fill_rect(origin_x + 60, origin_y + 78, 5, 1, current_theme.accent);
    fill_round_rect(window_x + 15, window_y + 318, 480, 50, 10, current_theme.surface_low);
    draw_text(window_x + 31, window_y + 339, "KEYBOARD  PS2", current_theme.text_muted, 1);
    draw_text(window_x + 207, window_y + 339, "SHELL  READY", current_theme.success, 1);
}

void render_terminal() {
    draw_window_chrome("ZEN TERMINAL", "LOCAL SESSION / I686");
    if (terminal_sync_started) render_terminal_cells(); else render_terminal_placeholder();
}

void draw_file_card(int32_t x, int32_t y, const char* name, const char* detail, uint32_t accent) {
    fill_round_rect(x, y, 156, 86, 13, current_theme.surface_high);
    fill_round_rect(x + 13, y + 14, 39, 33, 9, accent);
    fill_rect_alpha(x + 19, y + 10, 20, 9, rgb(255,255,255), 70);
    draw_text(x + 14, y + 58, name, current_theme.text, 1);
    draw_text(x + 14, y + 72, detail, current_theme.text_muted, 1);
}

void render_files() {
    draw_window_chrome("ZEN FILES", "/DATA / WORKSPACE");
    fill_round_rect(window_x + 14, window_y + 56, 104, 313, 11, current_theme.surface_low);
    draw_text(window_x + 30, window_y + 76, "PLACES", current_theme.text_muted, 1);
    fill_round_rect(window_x + 22, window_y + 96, 88, 30, 9, current_theme.accent_soft);
    draw_text(window_x + 34, window_y + 107, "HOME", current_theme.text, 1);
    draw_text(window_x + 34, window_y + 147, "APPS", current_theme.text_muted, 1);
    draw_text(window_x + 34, window_y + 187, "SYSTEM", current_theme.text_muted, 1);
    draw_text(window_x + 34, window_y + 227, "DOCS", current_theme.text_muted, 1);
    draw_text(window_x + 34, window_y + 267, "CONFIG", current_theme.text_muted, 1);
    draw_text(window_x + 140, window_y + 69, "WORKSPACE", current_theme.text, 2);
    draw_text(window_x + 141, window_y + 91, "7 APPLICATIONS / ZENOVFS1", current_theme.text_muted, 1);
    draw_file_card(window_x + 139, window_y + 116, "SYSTEM", "CORE FILES", rgb(74,156,231));
    draw_file_card(window_x + 309, window_y + 116, "APPS", "7 PACKAGES", rgb(139,124,246));
    draw_file_card(window_x + 139, window_y + 216, "DOCS", "REFERENCE", rgb(102,202,161));
    draw_file_card(window_x + 309, window_y + 216, "PROJECTS", "WORKSPACE", rgb(238,163,82));
    fill_round_rect(window_x + 139, window_y + 326, 326, 34, 10, current_theme.surface_low);
    fill_circle_alpha(window_x + 157, window_y + 343, 4, current_theme.success, 255);
    draw_text(window_x + 169, window_y + 339, "ZENOVFS MOUNTED / READ-WRITE", current_theme.text_muted, 1);
}

void draw_toggle(int32_t x, int32_t y, bool enabled) {
    fill_round_rect(x, y, 40, 20, 10, enabled ? current_theme.accent : current_theme.surface_low);
    fill_circle_alpha(x + (enabled ? 30 : 10), y + 10, 7, current_theme.text, 255);
}

void render_settings() {
    draw_window_chrome("SETTINGS", "APPEARANCE / DESKTOP / TERMINAL");
    draw_text(window_x + 24, window_y + 66, "APPEARANCE", current_theme.text_muted, 1);
    draw_text(window_x + 24, window_y + 91, "COLOR THEME", current_theme.text, 1);
    const uint32_t themes[3] = {rgb(139,124,246), rgb(126,142,177), rgb(245,174,82)};
    const char* names[3] = {"MIDNIGHT", "GRAPHITE", "AMBER"};
    for (uint32_t i = 0; i < 3U; ++i) {
        const int32_t x = window_x + 24 + static_cast<int32_t>(i * 148U);
        fill_round_rect(x, window_y + 109, 134, 70, 12, runtime_theme == i ? current_theme.accent_soft : current_theme.surface_high);
        fill_circle_alpha(x + 24, window_y + 133, 10, themes[i], 255);
        draw_text(x + 44, window_y + 129, names[i], current_theme.text, 1);
        draw_text(x + 44, window_y + 146, runtime_theme == i ? "ACTIVE" : "SELECT", runtime_theme == i ? current_theme.success : current_theme.text_muted, 1);
    }
    fill_round_rect(window_x + 24, window_y + 196, 462, 68, 12, current_theme.surface_high);
    draw_text(window_x + 42, window_y + 216, "ANIMATIONS", current_theme.text, 1);
    draw_text(window_x + 42, window_y + 234, desktop_animations ? "MOTION ENABLED" : "REDUCED MOTION", current_theme.text_muted, 1);
    draw_toggle(window_x + 422, window_y + 218, desktop_animations);
    fill_round_rect(window_x + 24, window_y + 278, 462, 81, 12, current_theme.surface_high);
    draw_text(window_x + 42, window_y + 298, "TERMINAL", current_theme.text, 1);
    draw_text(window_x + 42, window_y + 317, "LIVE VGA MIRROR / 80 X 25 / PS2 INPUT", current_theme.text_muted, 1);
    draw_text(window_x + 42, window_y + 339, "CONFIG: /DATA/CONFIG/UI.CFG", current_theme.accent, 1);
}

void render_launcher() {
    draw_window_chrome("APPLICATIONS", "TYPE TO SEARCH / ENTER TO OPEN");
    fill_round_rect(window_x + 24, window_y + 61, 462, 46, 14, current_theme.surface_low);
    draw_text(window_x + 43, window_y + 79, "SEARCH APPLICATIONS...", current_theme.text_muted, 1);
    const char letters[6] = {'T','F','S','I','D','A'};
    const char* names[6] = {"TERMINAL","FILES","SETTINGS","SYSTEM","DOCS","APPS"};
    const uint32_t accents[6] = {rgb(139,124,246),rgb(74,156,231),rgb(102,202,161),rgb(239,125,146),rgb(238,163,82),rgb(101,190,211)};
    for (uint32_t i = 0; i < 6U; ++i) {
        const int32_t column = static_cast<int32_t>(i % 3U), row = static_cast<int32_t>(i / 3U);
        const int32_t x = window_x + 24 + column * 151, y = window_y + 126 + row * 103;
        fill_round_rect(x, y, 138, 88, 13, current_theme.surface_high);
        fill_round_rect(x + 12, y + 12, 38, 38, 11, accents[i]);
        draw_char(x + 26, y + 23, letters[i], current_theme.text, 2);
        draw_text(x + 12, y + 61, names[i], current_theme.text, 1);
        draw_text(x + 12, y + 75, "ZENOV APP", current_theme.text_muted, 1);
    }
    draw_text(window_x + 24, window_y + 349, "6 BUILT-IN SURFACES / KEYBOARD-FIRST", current_theme.text_muted, 1);
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

