bool text_contains(const char* text, uint32_t size, const char* needle) {
    const uint32_t needle_size = static_cast<uint32_t>(string_length(needle));
    if (!needle_size || size < needle_size) return false;
    for (uint32_t i = 0; i + needle_size <= size; ++i) {
        bool match = true;
        for (uint32_t j = 0; j < needle_size; ++j) if (text[i + j] != needle[j]) { match = false; break; }
        if (match) return true;
    }
    return false;
}

void load_runtime_preferences() {
    if (string_equal(desktop_theme, "graphite")) apply_runtime_theme(1);
    else if (string_equal(desktop_theme, "amber")) apply_runtime_theme(2);
    else apply_runtime_theme(0);
    char data[128]{}; uint32_t size = 0;
    if (storage::read_file("/data/config/ui.cfg", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
        data[size] = 0;
        if (text_contains(data, size, "theme=graphite")) apply_runtime_theme(1);
        else if (text_contains(data, size, "theme=amber")) apply_runtime_theme(2);
        serial::line("UI_THEME_OVERRIDE_OK");
    } else serial::line("UI_THEME_DEFAULT_OK");
    runtime_preferences_loaded = true;
}

void persist_runtime_theme(uint8_t id) {
    const char* text = id == 1U ? "theme=graphite\n" : (id == 2U ? "theme=amber\n" : "theme=midnight\n");
    if (storage::write_file("/data/config/ui.cfg", reinterpret_cast<const uint8_t*>(text), static_cast<uint32_t>(string_length(text)), false)) serial::line("UI_SETTINGS_PERSIST_OK");
    else serial::line("UI_SETTINGS_PERSIST_FAILED");
}

void draw_wallpaper() {
    vertical_gradient(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height), current_theme.canvas_top, current_theme.canvas_bottom);
    fill_circle_alpha(126, 104, 190, current_theme.glow_primary, 29);
    fill_circle_alpha(685, 430, 225, current_theme.glow_secondary, 24);
    fill_circle_alpha(610, 74, 110, current_theme.accent, 17);
    for (uint32_t i = 0; i < 38U; ++i) {
        const int32_t x = static_cast<int32_t>((i * 149U + 37U) % width);
        const int32_t y = static_cast<int32_t>((i * 83U + 61U) % height);
        fill_rect_alpha(x, y, i % 5U == 0U ? 2 : 1, i % 5U == 0U ? 2 : 1, current_theme.text, static_cast<uint8_t>(18U + (i % 3U) * 8U));
    }
}

void draw_pill(int32_t x, int32_t y, int32_t pill_width, const char* label, bool selected) {
    const uint32_t fill = selected ? current_theme.accent : current_theme.surface_high;
    fill_round_rect(x, y, pill_width, 26, 13, fill);
    draw_text(x + 12, y + 9, label, selected ? rgb(255,255,255) : current_theme.text_muted, 1);
}

void draw_topbar() {
    fill_round_rect_alpha(16, 12, 768, 42, 18, current_theme.surface_low, desktop_opacity);
    draw_text(34, 27, "ZENOVOS", current_theme.text, 1);
    fill_round_rect(94, 24, 5, 5, 3, current_theme.success);
    draw_text(108, 27, "LOCAL", current_theme.text_muted, 1);
    draw_pill(310, 20, 52, "01", true);
    draw_pill(368, 20, 52, "02", false);
    draw_pill(426, 20, 52, "03", false);
    fill_round_rect(632, 20, 68, 26, 13, current_theme.surface_high);
    draw_text(644, 29, "UP", current_theme.text_muted, 1);
    draw_uint(664, 29, timer_ticks / 100U, current_theme.text);
    fill_round_rect(710, 20, 56, 26, 13, current_theme.accent_soft);
    fill_circle_alpha(727, 33, 4, current_theme.success, 255);
    draw_text(738, 29, "OK", current_theme.text, 1);
}

void draw_dock_icon(int32_t x, char letter, const char* tooltip, bool selected, uint32_t accent) {
    if (selected) fill_round_rect_alpha(x - 5, 541, 52, 48, 17, current_theme.accent, 48);
    fill_round_rect(x, 546, 38, 38, 12, selected ? accent : current_theme.surface_high);
    draw_char(x + 14, 557, letter, current_theme.text, 2);
    if (selected) fill_round_rect(x + 13, 590, 12, 3, 2, current_theme.accent);
    (void)tooltip;
}

void draw_dock() {
    fill_round_rect_alpha(222, 535, 356, 61, 22, current_theme.surface_low, desktop_opacity);
    draw_dock_icon(243, 'T', "TERMINAL", active_view == View::terminal, current_theme.accent);
    draw_dock_icon(303, 'F', "FILES", active_view == View::files, rgb(74,156,231));
    draw_dock_icon(363, 'S', "SETTINGS", active_view == View::settings, rgb(102,202,161));
    draw_dock_icon(423, 'A', "APPS", active_view == View::launcher, rgb(238,163,82));
    fill_rect_alpha(478, 548, 1, 34, current_theme.border, 180);
    fill_round_rect(495, 546, 62, 38, 12, current_theme.surface_high);
    fill_circle_alpha(511, 565, 4, current_theme.success, 255);
    draw_text(522, 561, "READY", current_theme.text_muted, 1);
}

void draw_window_chrome(const char* title, const char* subtitle) {
    const int32_t radius = static_cast<int32_t>(desktop_radius);
    draw_shadow(window_x, window_y, window_width, window_height, radius);
    fill_round_rect(window_x, window_y, window_width, window_height, radius, current_theme.surface);
    draw_round_border(window_x, window_y, window_width, window_height, radius, current_theme.border);
    fill_round_rect(window_x + 1, window_y + 1, window_width - 2, titlebar_height + 8, radius > 1 ? radius - 1 : 0, current_theme.surface_high);
    fill_rect(window_x + 1, window_y + titlebar_height, window_width - 2, 9, current_theme.surface_high);
    fill_circle_alpha(window_x + 22, window_y + 21, 5, current_theme.danger, 255);
    fill_circle_alpha(window_x + 40, window_y + 21, 5, current_theme.warning, 255);
    fill_circle_alpha(window_x + 58, window_y + 21, 5, current_theme.success, 255);
    draw_text(window_x + 82, window_y + 14, title, current_theme.text, 1);
    draw_text(window_x + 82, window_y + 27, subtitle, current_theme.text_muted, 1);
}

constexpr uint32_t vga_colors[16] = {
    rgb(10,12,20), rgb(66,96,180), rgb(72,166,118), rgb(74,166,176),
    rgb(190,72,82), rgb(157,91,193), rgb(201,132,57), rgb(200,207,220),
    rgb(93,101,120), rgb(111,143,232), rgb(113,217,168), rgb(102,203,220),
    rgb(239,125,146), rgb(202,151,238), rgb(242,198,109), rgb(244,246,255)
};

