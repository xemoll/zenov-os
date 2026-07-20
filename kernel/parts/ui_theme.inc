enum class HoverTarget : uint8_t {
    none,
    taskbar_start, taskbar_terminal, taskbar_files, taskbar_settings, taskbar_tray,
    quick_theme, quick_display, quick_motion, quick_cursor, quick_density, quick_taskbar,
    settings_page_general, settings_page_style, settings_page_about,
    theme_midnight, theme_graphite, theme_amber,
    settings_display, settings_motion, settings_cursor,
    settings_accent, settings_wallpaper, settings_radius, settings_opacity,
    settings_density, settings_taskbar, settings_reset,
    launcher_terminal, launcher_files, launcher_settings, launcher_display
};

HoverTarget hovered_target = HoverTarget::none;
uint8_t launcher_focus = 0;
uint8_t settings_focus = 0;
uint8_t settings_page = 0;
uint8_t quick_settings_focus = 0;
bool quick_settings_open = false;
bool scene_transition_pending = false;
int32_t ui_scene_offset_y = 0;
char launcher_query[18]{};
uint8_t launcher_query_length = 0;

constexpr uint8_t settings_page_count = 3U;
constexpr uint8_t launcher_item_count = 4U;
constexpr uint8_t quick_settings_count = 6U;
constexpr uint8_t accent_palette_size = 6U;
uint8_t runtime_accent_slot = 0;
uint8_t runtime_wallpaper_style = 0;
bool runtime_compact_density = false;
bool runtime_taskbar_left = false;
uint32_t runtime_base_accent = desktop_accent;

bool is_hovered(HoverTarget target) { return hovered_target == target; }
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

bool parse_display_number(const char*& value, uint32_t& result) {
    if (!value || *value < '0' || *value > '9') return false;
    result = 0;
    uint32_t digits = 0;
    while (*value >= '0' && *value <= '9' && digits < 5U) {
        result = result * 10U + static_cast<uint32_t>(*value - '0');
        ++value; ++digits;
    }
    return digits != 0U;
}

bool parse_display_mode(const char* text, uint32_t size, uint8_t& mode) {
    const char* value = config_value(text, size, "resolution");
    uint32_t parsed_width = 0, parsed_height = 0;
    if (!parse_display_number(value, parsed_width) || (*value != 'x' && *value != 'X')) return false;
    ++value;
    if (!parse_display_number(value, parsed_height)) return false;
    if (*value && *value != '\n' && *value != '\r') return false;
    for (uint8_t index = 0; index < display_mode_count; ++index) {
        if (display_modes[index].width == parsed_width && display_modes[index].height == parsed_height) {
            mode = index;
            return true;
        }
    }
    return false;
}

uint32_t accent_palette_color(uint8_t slot) {
    static constexpr uint32_t colors[accent_palette_size] = {
        0U, rgb(80, 142, 255), rgb(133, 103, 255), rgb(35, 184, 190), rgb(70, 183, 123), rgb(230, 113, 103)
    };
    return slot == 0U || slot >= accent_palette_size ? runtime_base_accent : colors[slot];
}

void apply_accent_personalization() {
    current_theme.accent = accent_palette_color(runtime_accent_slot);
    current_theme.accent_soft = mix(current_theme.accent, current_theme.surface, 2U, 5U);
    current_theme.glow_primary = mix(current_theme.accent, current_theme.canvas_top, 1U, 3U);
}

bool apply_config_text(const char* text, uint32_t size, bool& custom_colors) {
    if (!text || !size) return false;
    bool recognized = false;
    if (text_contains(text, size, "theme=graphite")) { desktop_theme = "graphite"; recognized = true; }
    else if (text_contains(text, size, "theme=amber")) { desktop_theme = "amber"; recognized = true; }
    else if (text_contains(text, size, "theme=midnight") || text_contains(text, size, "theme=zen-dark")) { desktop_theme = "midnight"; recognized = true; }

    uint8_t parsed_mode = requested_display_mode;
    if (parse_display_mode(text, size, parsed_mode)) { requested_display_mode = parsed_mode; recognized = true; }

    uint32_t parsed = 0;
    bool any_color = false;
    if (parse_hex_color(config_value(text, size, "canvas"), parsed)) { desktop_canvas = parsed; any_color = true; recognized = true; }
    if (parse_hex_color(config_value(text, size, "surface"), parsed)) { desktop_surface = parsed; any_color = true; recognized = true; }
    if (parse_hex_color(config_value(text, size, "accent"), parsed)) { desktop_accent = parsed; any_color = true; recognized = true; }
    if (any_color) custom_colors = true;

    if (parse_decimal_value(config_value(text, size, "accent_slot"), parsed) && parsed < accent_palette_size) {
        runtime_accent_slot = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (text_contains(text, size, "wallpaper=calm")) { runtime_wallpaper_style = 1U; recognized = true; }
    else if (text_contains(text, size, "wallpaper=plain")) { runtime_wallpaper_style = 2U; recognized = true; }
    else if (text_contains(text, size, "wallpaper=flow")) { runtime_wallpaper_style = 0U; recognized = true; }

    if (parse_decimal_value(config_value(text, size, "radius"), parsed) && parsed >= 6U && parsed <= 20U) {
        desktop_radius = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (parse_decimal_value(config_value(text, size, "opacity"), parsed) && parsed >= 176U && parsed <= 255U) {
        desktop_opacity = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (parse_decimal_value(config_value(text, size, "animations"), parsed) && parsed <= 1U) {
        desktop_animations = parsed != 0U; recognized = true;
    }
    if (text_contains(text, size, "cursor=block")) { terminal_cursor = "block"; recognized = true; }
    else if (text_contains(text, size, "cursor=underline")) { terminal_cursor = "underline"; recognized = true; }
    else if (text_contains(text, size, "cursor=beam")) { terminal_cursor = "beam"; recognized = true; }
    if (text_contains(text, size, "density=compact")) { runtime_compact_density = true; recognized = true; }
    else if (text_contains(text, size, "density=comfortable")) { runtime_compact_density = false; recognized = true; }
    if (text_contains(text, size, "taskbar=left")) { runtime_taskbar_left = true; recognized = true; }
    else if (text_contains(text, size, "taskbar=center")) { runtime_taskbar_left = false; recognized = true; }
    return recognized;
}

void materialize_theme(bool custom_colors) {
    if (string_equal(desktop_theme, "graphite")) apply_runtime_theme(1);
    else if (string_equal(desktop_theme, "amber")) apply_runtime_theme(2);
    else apply_runtime_theme(0);
    if (custom_colors) {
        current_theme.canvas_top = desktop_canvas;
        current_theme.canvas_bottom = mix(desktop_canvas, desktop_surface, 3U, 8U);
        current_theme.surface = desktop_surface;
        current_theme.surface_high = mix(desktop_surface, rgb(255,255,255), 22U, 255U);
        current_theme.surface_low = mix(desktop_surface, rgb(0,0,0), 58U, 255U);
        current_theme.accent = desktop_accent;
        current_theme.accent_soft = mix(desktop_accent, desktop_surface, 2U, 5U);
    }
    runtime_base_accent = current_theme.accent;
    apply_accent_personalization();
}

void select_runtime_theme(uint8_t id) {
    apply_runtime_theme(id);
    desktop_theme = runtime_theme == 1U ? "graphite" : (runtime_theme == 2U ? "amber" : "midnight");
    runtime_base_accent = current_theme.accent;
    apply_accent_personalization();
}

void load_runtime_preferences() {
    bool custom_colors = false;
    const char* builtin = nullptr; uint32_t builtin_size = 0;
    if (read_desktop_config(builtin, builtin_size) && apply_config_text(builtin, builtin_size, custom_colors)) serial::line("UI_ZENOV_CONFIG_OK");
    else serial::line("UI_ZENOV_CONFIG_FALLBACK");

    char data[384]{}; uint32_t size = 0;
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

bool append_config_text(char* output, uint32_t capacity, uint32_t& length, const char* text) {
    while (text && *text) {
        if (length + 1U >= capacity) return false;
        output[length++] = *text++;
    }
    output[length] = 0;
    return true;
}

bool append_config_uint(char* output, uint32_t capacity, uint32_t& length, uint32_t value) {
    char reverse[11]; uint32_t count = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    while (count) {
        if (length + 1U >= capacity) return false;
        output[length++] = reverse[--count];
    }
    output[length] = 0;
    return true;
}

bool persist_runtime_preferences() {
    char text[384]{}; uint32_t length = 0;
    const char* theme_name = runtime_theme == 1U ? "graphite" : (runtime_theme == 2U ? "amber" : "midnight");
    const char* wallpaper_name = runtime_wallpaper_style == 1U ? "calm" : (runtime_wallpaper_style == 2U ? "plain" : "flow");
    const DisplayMode mode = display_modes[active_display_mode];
    if (!append_config_text(text, sizeof(text), length, "theme=") ||
        !append_config_text(text, sizeof(text), length, theme_name) ||
        !append_config_text(text, sizeof(text), length, "\nresolution=") ||
        !append_config_uint(text, sizeof(text), length, mode.width) ||
        !append_config_text(text, sizeof(text), length, "x") ||
        !append_config_uint(text, sizeof(text), length, mode.height) ||
        !append_config_text(text, sizeof(text), length, "\nanimations=") ||
        !append_config_text(text, sizeof(text), length, desktop_animations ? "1\n" : "0\n") ||
        !append_config_text(text, sizeof(text), length, "cursor=") ||
        !append_config_text(text, sizeof(text), length, terminal_cursor) ||
        !append_config_text(text, sizeof(text), length, "\naccent_slot=") ||
        !append_config_uint(text, sizeof(text), length, runtime_accent_slot) ||
        !append_config_text(text, sizeof(text), length, "\nwallpaper=") ||
        !append_config_text(text, sizeof(text), length, wallpaper_name) ||
        !append_config_text(text, sizeof(text), length, "\nradius=") ||
        !append_config_uint(text, sizeof(text), length, desktop_radius) ||
        !append_config_text(text, sizeof(text), length, "\nopacity=") ||
        !append_config_uint(text, sizeof(text), length, desktop_opacity) ||
        !append_config_text(text, sizeof(text), length, "\ndensity=") ||
        !append_config_text(text, sizeof(text), length, runtime_compact_density ? "compact" : "comfortable") ||
        !append_config_text(text, sizeof(text), length, "\ntaskbar=") ||
        !append_config_text(text, sizeof(text), length, runtime_taskbar_left ? "left\n" : "center\n")) return false;
    return storage::write_file("/data/config/ui.cfg", reinterpret_cast<const uint8_t*>(text), length, false);
}

void draw_wallpaper() {
    vertical_gradient(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height), current_theme.canvas_top, current_theme.canvas_bottom);
    if (runtime_wallpaper_style == 2U) {
        fill_rect_alpha(0, 0, static_cast<int32_t>(width), 1, current_theme.accent, 40);
        return;
    }
    if (runtime_wallpaper_style == 1U) {
        fill_circle_alpha(684, 100, 180, current_theme.glow_secondary, 8);
        fill_circle_alpha(118, 520, 220, current_theme.glow_primary, 7);
        return;
    }
    fill_circle_alpha(112, 70, 230, current_theme.glow_primary, 12);
    fill_circle_alpha(716, 522, 286, current_theme.glow_secondary, 10);
    fill_circle_alpha(666, 82, 145, current_theme.accent, 5);
    fill_rect_alpha(0, 0, static_cast<int32_t>(width), 1, current_theme.accent, 46);
}

void draw_topbar() {
    draw_text(topbar_x, topbar_y + 8, "ZENOVOS", current_theme.text_muted, 1);
    fill_circle_alpha(topbar_x + 58, topbar_y + 11, 2, current_theme.success, 255);
    draw_text(topbar_x + 68, topbar_y + 8, "LOCAL", current_theme.text_muted, 1);
    draw_text_right(topbar_x + topbar_width, topbar_y + 8, zenov_generated::kVersion, current_theme.text_muted, 1);
}

enum class ShellIcon : uint8_t { start, terminal, files, settings, apps, display };

void draw_shell_icon(ShellIcon icon, int32_t x, int32_t y, uint32_t color) {
    if (icon == ShellIcon::start) {
        fill_rect(x + 1, y + 1, 8, 8, color); fill_rect(x + 12, y + 1, 8, 8, color);
        fill_rect(x + 1, y + 12, 8, 8, color); fill_rect(x + 12, y + 12, 8, 8, color); return;
    }
    if (icon == ShellIcon::terminal) {
        draw_char(x, y + 2, '>', color, 1); fill_rect(x + 10, y + 14, 10, 2, color); return;
    }
    if (icon == ShellIcon::files) {
        fill_round_rect(x + 1, y + 6, 21, 15, 3, color); fill_round_rect(x + 3, y + 2, 10, 8, 2, color); return;
    }
    if (icon == ShellIcon::settings) {
        fill_rect(x + 1, y + 3, 7, 2, color); fill_rect(x + 12, y + 3, 10, 2, color); fill_rect(x + 8, y + 1, 4, 6, color);
        fill_rect(x + 1, y + 10, 13, 2, color); fill_rect(x + 18, y + 10, 4, 2, color); fill_rect(x + 14, y + 8, 4, 6, color);
        fill_rect(x + 1, y + 17, 3, 2, color); fill_rect(x + 8, y + 17, 14, 2, color); fill_rect(x + 4, y + 15, 4, 6, color); return;
    }
    if (icon == ShellIcon::display) {
        fill_round_rect(x + 1, y + 2, 22, 15, 3, color); fill_rect(x + 10, y + 17, 4, 4, color); fill_rect(x + 6, y + 21, 12, 2, color); return;
    }
    fill_round_rect(x + 1, y + 1, 8, 8, 2, color); fill_round_rect(x + 13, y + 1, 8, 8, 2, color);
    fill_round_rect(x + 1, y + 13, 8, 8, 2, color); fill_round_rect(x + 13, y + 13, 8, 8, 2, color);
}

void draw_window_chrome(const char* title, const char* subtitle) {
    const int32_t radius = static_cast<int32_t>(desktop_radius);
    draw_shadow(window_x, window_y, window_width, window_height, radius);
    fill_round_rect(window_x, window_y, window_width, window_height, radius, current_theme.surface);
    draw_round_border(window_x, window_y, window_width, window_height, radius, current_theme.border);
    fill_round_rect(window_x + 1, window_y + 1, window_width - 2, titlebar_height + 7, radius > 1 ? radius - 1 : 0, current_theme.surface_high);
    fill_rect(window_x + 1, window_y + titlebar_height, window_width - 2, 8, current_theme.surface_high);
    fill_round_rect(window_x + 14, window_y + 11, 24, 24, 6, current_theme.accent_soft);
    draw_shell_icon(active_view == View::terminal ? ShellIcon::terminal : (active_view == View::files ? ShellIcon::files : ShellIcon::settings),
                    window_x + 16, window_y + 13, current_theme.accent);
    draw_text(window_x + 50, window_y + 17, title, current_theme.text, 1);
    draw_text_right(window_x + window_width - 18, window_y + 17, subtitle, current_theme.text_muted, 1);
    fill_rect(window_x + 16, window_y + titlebar_height, window_width - 32, 1, current_theme.border);
}

constexpr uint32_t vga_colors[16] = {
    rgb(10,12,20), rgb(66,96,180), rgb(72,166,118), rgb(74,166,176),
    rgb(190,72,82), rgb(157,91,193), rgb(201,132,57), rgb(200,207,220),
    rgb(93,101,120), rgb(111,143,232), rgb(113,217,168), rgb(102,203,220),
    rgb(239,125,146), rgb(202,151,238), rgb(242,198,109), rgb(244,246,255)
};
