enum class HoverTarget : uint8_t {
    none,
    workspace_one, workspace_two, workspace_three,
    dock_terminal, dock_files, dock_settings, dock_launcher,
    theme_midnight, theme_graphite, theme_amber,
    launcher_terminal, launcher_files, launcher_settings
};

HoverTarget hovered_target = HoverTarget::none;
uint8_t launcher_focus = 0;
uint8_t settings_focus = 0;
constexpr int32_t workspace_item_width = 32;
constexpr int32_t workspace_group_width = workspace_item_width * 3;
constexpr int32_t workspace_group_x = static_cast<int32_t>(width / 2U) - workspace_group_width / 2;
constexpr int32_t workspace_group_y = topbar_y + 6;
constexpr int32_t workspace_group_height = 24;

bool is_hovered(HoverTarget target) { return hovered_target == target; }
uint8_t workspace_for_view(View view) {
    if (view == View::terminal) return 0;
    if (view == View::files) return 1;
    return 2;
}

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

const char* config_value(const char* text, uint32_t size, const char* key) {
    const uint32_t key_size = static_cast<uint32_t>(string_length(key));
    if (!key_size || size <= key_size) return nullptr;
    for (uint32_t i = 0; i + key_size + 1U <= size; ++i) {
        if (i && text[i - 1U] != '\n' && text[i - 1U] != '\r') continue;
        bool match = true;
        for (uint32_t j = 0; j < key_size; ++j) if (text[i + j] != key[j]) { match = false; break; }
        if (match && text[i + key_size] == '=') return text + i + key_size + 1U;
    }
    return nullptr;
}

bool parse_decimal_value(const char* value, uint32_t& result) {
    if (!value || *value < '0' || *value > '9') return false;
    uint32_t parsed = 0, digits = 0;
    while (*value >= '0' && *value <= '9' && digits < 10U) {
        parsed = parsed * 10U + static_cast<uint32_t>(*value - '0');
        ++value; ++digits;
    }
    if (*value && *value != '\n' && *value != '\r') return false;
    result = parsed;
    return true;
}

bool parse_hex_color(const char* value, uint32_t& result) {
    if (!value) return false;
    if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value += 2;
    uint32_t parsed = 0;
    for (uint32_t i = 0; i < 6U; ++i) {
        const char c = value[i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
        else return false;
        parsed = (parsed << 4U) | digit;
    }
    if (value[6] && value[6] != '\n' && value[6] != '\r') return false;
    result = parsed;
    return true;
}

bool read_desktop_config(const char*& text, uint32_t& size) {
    for (uint32_t i = 0; i < zenov_generated::kFileCount; ++i) {
        if (!string_equal(zenov_generated::kFiles[i].name, "DESKTOP.CFG")) continue;
        text = zenov_generated::kFiles[i].text;
        size = static_cast<uint32_t>(string_length(text));
        return true;
    }
    return false;
}

bool apply_config_text(const char* text, uint32_t size, bool& custom_colors) {
    if (!text || !size) return false;
    bool recognized = false;
    if (text_contains(text, size, "theme=graphite")) { desktop_theme = "graphite"; recognized = true; }
    else if (text_contains(text, size, "theme=amber")) { desktop_theme = "amber"; recognized = true; }
    else if (text_contains(text, size, "theme=midnight") || text_contains(text, size, "theme=zen-dark")) { desktop_theme = "midnight"; recognized = true; }

    uint32_t parsed = 0;
    bool any_color = false;
    if (parse_hex_color(config_value(text, size, "canvas"), parsed)) { desktop_canvas = parsed; any_color = true; recognized = true; }
    if (parse_hex_color(config_value(text, size, "surface"), parsed)) { desktop_surface = parsed; any_color = true; recognized = true; }
    if (parse_hex_color(config_value(text, size, "accent"), parsed)) { desktop_accent = parsed; any_color = true; recognized = true; }
    if (any_color) custom_colors = true;

    if (parse_decimal_value(config_value(text, size, "radius"), parsed) && parsed >= 4U && parsed <= 24U) {
        desktop_radius = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (parse_decimal_value(config_value(text, size, "opacity"), parsed) && parsed >= 128U && parsed <= 255U) {
        desktop_opacity = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (parse_decimal_value(config_value(text, size, "animations"), parsed) && parsed <= 1U) {
        desktop_animations = parsed != 0U; recognized = true;
    }
    if (text_contains(text, size, "cursor=block")) { terminal_cursor = "block"; recognized = true; }
    else if (text_contains(text, size, "cursor=underline")) { terminal_cursor = "underline"; recognized = true; }
    else if (text_contains(text, size, "cursor=beam")) { terminal_cursor = "beam"; recognized = true; }
    return recognized;
}

void materialize_theme(bool custom_colors) {
    if (string_equal(desktop_theme, "graphite")) apply_runtime_theme(1);
    else if (string_equal(desktop_theme, "amber")) apply_runtime_theme(2);
    else apply_runtime_theme(0);
    if (!custom_colors) return;
    current_theme.canvas_top = desktop_canvas;
    current_theme.canvas_bottom = mix(desktop_canvas, desktop_surface, 3U, 8U);
    current_theme.surface = desktop_surface;
    current_theme.surface_high = mix(desktop_surface, rgb(255,255,255), 26U, 255U);
    current_theme.surface_low = mix(desktop_surface, rgb(0,0,0), 55U, 255U);
    current_theme.accent = desktop_accent;
    current_theme.accent_soft = mix(desktop_accent, desktop_surface, 3U, 5U);
}

void load_runtime_preferences() {
    bool custom_colors = false;
    const char* builtin = nullptr; uint32_t builtin_size = 0;
    if (read_desktop_config(builtin, builtin_size) && apply_config_text(builtin, builtin_size, custom_colors)) {
        serial::line("UI_ZENOV_CONFIG_OK");
    } else {
        serial::line("UI_ZENOV_CONFIG_FALLBACK");
    }

    char data[256]{}; uint32_t size = 0;
    if (storage::read_file("/data/config/ui.cfg", reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
        data[size] = 0;
        const bool runtime_has_colors = config_value(data, size, "canvas") || config_value(data, size, "surface") || config_value(data, size, "accent");
        const bool runtime_has_theme = config_value(data, size, "theme") != nullptr;
        if (runtime_has_theme && !runtime_has_colors) custom_colors = false;
        apply_config_text(data, size, custom_colors);
        serial::line("UI_THEME_OVERRIDE_OK");
    } else serial::line("UI_THEME_DEFAULT_OK");
    materialize_theme(custom_colors);
    runtime_preferences_loaded = true;
}

void persist_runtime_theme(uint8_t id) {
    const char* text = id == 1U ? "theme=graphite\n" : (id == 2U ? "theme=amber\n" : "theme=midnight\n");
    if (storage::write_file("/data/config/ui.cfg", reinterpret_cast<const uint8_t*>(text), static_cast<uint32_t>(string_length(text)), false)) serial::line("UI_SETTINGS_PERSIST_OK");
    else serial::line("UI_SETTINGS_PERSIST_FAILED");
}

void draw_wallpaper() {
    vertical_gradient(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height), current_theme.canvas_top, current_theme.canvas_bottom);
    fill_circle_alpha(400, -112, 352, current_theme.glow_primary, 11);
}

void draw_workspace_switcher() {
    const HoverTarget targets[3] = {HoverTarget::workspace_one, HoverTarget::workspace_two, HoverTarget::workspace_three};
    const uint8_t active_workspace = workspace_for_view(active_view);
    fill_round_rect(workspace_group_x, workspace_group_y, workspace_group_width, workspace_group_height, 12, current_theme.surface_high);
    for (uint32_t i = 0; i < 3U; ++i) {
        const int32_t x = workspace_group_x + static_cast<int32_t>(i * workspace_item_width);
        if (i == active_workspace) fill_round_rect(x + 2, workspace_group_y + 2, workspace_item_width - 4, 20, 10, current_theme.accent);
        else if (is_hovered(targets[i])) fill_round_rect(x + 2, workspace_group_y + 2, workspace_item_width - 4, 20, 10, current_theme.accent_soft);
        const char label[2] = {static_cast<char>('1' + i), 0};
        draw_text_center(x + workspace_item_width / 2, workspace_group_y + 9, label,
                         i == active_workspace ? rgb(255,255,255) : current_theme.text_muted, 1);
    }
}

void draw_topbar() {
    fill_round_rect_alpha(topbar_x, topbar_y, topbar_width, topbar_height, 14, current_theme.surface_low, desktop_opacity);
    draw_text(topbar_x + 16, topbar_y + 14, "ZENOVOS", current_theme.text, 1);
    draw_workspace_switcher();
    draw_text_right(topbar_x + topbar_width - 16, topbar_y + 14, zenov_generated::kVersion, current_theme.text_muted, 1);
}

enum class ShellIcon : uint8_t { terminal, files, settings, apps };

void draw_shell_icon(ShellIcon icon, int32_t x, int32_t y, uint32_t color) {
    if (icon == ShellIcon::terminal) {
        draw_char(x, y + 2, '>', color, 1);
        fill_rect(x + 10, y + 14, 10, 2, color);
        return;
    }
    if (icon == ShellIcon::files) {
        fill_round_rect(x + 1, y + 6, 21, 15, 3, color);
        fill_round_rect(x + 3, y + 2, 10, 8, 2, color);
        return;
    }
    if (icon == ShellIcon::settings) {
        fill_rect(x + 1, y + 3, 7, 2, color);
        fill_rect(x + 12, y + 3, 10, 2, color);
        fill_rect(x + 8, y + 1, 4, 6, color);
        fill_rect(x + 1, y + 10, 13, 2, color);
        fill_rect(x + 18, y + 10, 4, 2, color);
        fill_rect(x + 14, y + 8, 4, 6, color);
        fill_rect(x + 1, y + 17, 3, 2, color);
        fill_rect(x + 8, y + 17, 14, 2, color);
        fill_rect(x + 4, y + 15, 4, 6, color);
        return;
    }
    fill_round_rect(x + 1, y + 1, 8, 8, 2, color);
    fill_round_rect(x + 13, y + 1, 8, 8, 2, color);
    fill_round_rect(x + 1, y + 13, 8, 8, 2, color);
    fill_round_rect(x + 13, y + 13, 8, 8, 2, color);
}

void draw_dock_icon(int32_t x, ShellIcon icon, bool selected, uint32_t accent, HoverTarget target) {
    uint32_t fill = current_theme.surface_high;
    if (selected) fill = accent;
    else if (is_hovered(target)) fill = mix(current_theme.surface_high, accent, 1U, 4U);
    const uint32_t icon_color = selected ? rgb(255,255,255) : current_theme.text_muted;
    fill_round_rect(x, dock_y + 6, 40, 40, 11, fill);
    draw_shell_icon(icon, x + 9, dock_y + 15, icon_color);
    if (selected) fill_round_rect(x + 14, dock_y + dock_height - 3, 12, 2, 1, accent);
}

void draw_dock() {
    fill_round_rect_alpha(dock_x, dock_y, dock_width, dock_height, 17, current_theme.surface_low, desktop_opacity);
    draw_dock_icon(dock_x + 12, ShellIcon::terminal, active_view == View::terminal, current_theme.accent, HoverTarget::dock_terminal);
    draw_dock_icon(dock_x + 68, ShellIcon::files, active_view == View::files, rgb(74,156,231), HoverTarget::dock_files);
    draw_dock_icon(dock_x + 124, ShellIcon::settings, active_view == View::settings, rgb(102,202,161), HoverTarget::dock_settings);
    draw_dock_icon(dock_x + 180, ShellIcon::apps, active_view == View::launcher, rgb(238,163,82), HoverTarget::dock_launcher);
}

void draw_window_chrome(const char* title, const char* subtitle) {
    const int32_t radius = static_cast<int32_t>(desktop_radius);
    draw_shadow(window_x, window_y, window_width, window_height, radius);
    fill_round_rect(window_x, window_y, window_width, window_height, radius, current_theme.surface);
    draw_round_border(window_x, window_y, window_width, window_height, radius, current_theme.border);
    fill_round_rect(window_x + 1, window_y + 1, window_width - 2, titlebar_height + 7, radius > 1 ? radius - 1 : 0, current_theme.surface_high);
    fill_rect(window_x + 1, window_y + titlebar_height, window_width - 2, 8, current_theme.surface_high);
    fill_round_rect(window_x + 16, window_y + 12, 3, 16, 2, current_theme.accent);
    draw_text(window_x + 30, window_y + 17, title, current_theme.text, 1);
    draw_text_right(window_x + window_width - 20, window_y + 17, subtitle, current_theme.text_muted, 1);
    fill_rect(window_x + 16, window_y + titlebar_height, window_width - 32, 1, current_theme.border);
}

constexpr uint32_t vga_colors[16] = {
    rgb(10,12,20), rgb(66,96,180), rgb(72,166,118), rgb(74,166,176),
    rgb(190,72,82), rgb(157,91,193), rgb(201,132,57), rgb(200,207,220),
    rgb(93,101,120), rgb(111,143,232), rgb(113,217,168), rgb(102,203,220),
    rgb(239,125,146), rgb(202,151,238), rgb(242,198,109), rgb(244,246,255)
};
