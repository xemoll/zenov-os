enum class HoverTarget : uint8_t {
    none,
    taskbar_start, taskbar_terminal, taskbar_files, taskbar_settings, taskbar_tray, taskbar_status, taskbar_clock,
    quick_theme, quick_display, quick_motion, quick_contrast, quick_density, quick_taskbar,
    settings_page_general, settings_page_style, settings_page_access, settings_page_about,
    theme_midnight, theme_graphite, theme_amber,
    settings_display, settings_motion, settings_cursor,
    settings_accent, settings_wallpaper, settings_radius, settings_opacity,
    settings_density, settings_taskbar, settings_reset,
    settings_contrast, settings_pointer, settings_access_motion,
    launcher_terminal, launcher_files, launcher_settings, launcher_display, launcher_packages, launcher_security
};

HoverTarget hovered_target = HoverTarget::none;
uint8_t launcher_focus = 0;
uint8_t settings_focus = 0;
uint8_t settings_page = 0;
uint8_t quick_settings_focus = 0;
bool quick_settings_open = false;
bool status_center_open = false;
bool scene_transition_pending = false;
int32_t ui_scene_offset_y = 0;
char launcher_query[18]{};
uint8_t launcher_query_length = 0;

constexpr uint8_t settings_page_count = 4U;
constexpr uint8_t launcher_item_count = 6U;
constexpr uint8_t quick_settings_count = 6U;
constexpr uint8_t accent_palette_size = 6U;
uint8_t runtime_accent_slot = 0;
uint8_t runtime_wallpaper_style = 0;
bool runtime_compact_density = false;
bool runtime_taskbar_left = false;
bool runtime_high_contrast = false;
bool runtime_large_pointer = false;
bool keyboard_focus_mode = false;
uint32_t runtime_base_accent = desktop_accent;

bool is_hovered(HoverTarget target) { return !keyboard_focus_mode && hovered_target == target; }

const char* config_value(const char* text, uint32_t size, const char* key) {
    const uint32_t key_size = static_cast<uint32_t>(string_length(key));
    if (!text || !key_size || size <= key_size) return nullptr;
    const char* found = nullptr;
    for (uint32_t i = 0; i + key_size + 1U <= size; ++i) {
        if (i && text[i - 1U] != '\n' && text[i - 1U] != '\r') continue;
        bool match = true;
        for (uint32_t j = 0; j < key_size; ++j) if (text[i + j] != key[j]) { match = false; break; }
        if (!match || text[i + key_size] != '=') continue;
        if (found) return nullptr;
        found = text + i + key_size + 1U;
    }
    return found;
}

bool config_value_equal(const char* text, uint32_t size, const char* key, const char* expected) {
    const char* value = config_value(text, size, key);
    if (!value || !expected) return false;
    const uint32_t offset = static_cast<uint32_t>(value - text);
    uint32_t index = 0U;
    while (expected[index]) {
        if (offset + index >= size || value[index] != expected[index]) return false;
        ++index;
    }
    return offset + index == size || value[index] == 0 || value[index] == '\n' || value[index] == '\r';
}

bool parse_decimal_value(const char* value, uint32_t& result) {
    if (!value || *value < '0' || *value > '9') return false;
    uint32_t parsed = 0, digits = 0;
    while (*value >= '0' && *value <= '9' && digits < 10U) {
        const uint32_t digit = static_cast<uint32_t>(*value - '0');
        if (parsed > (0xFFFFFFFFU - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++value; ++digits;
    }
    if (*value && *value != '\n' && *value != '\r') return false;
    result = parsed;
    return true;
}

bool config_parser_contract_ok() {
    static constexpr char valid[] = "theme=graphite\nradius=12\n";
    static constexpr char unrelated[] = "not_theme=graphite\n";
    static constexpr char overflow[] = "radius=4294967296\n";
    static constexpr char duplicate[] = "theme=graphite\ntheme=amber\n";
    uint32_t parsed = 0U;
    return config_value_equal(valid, sizeof(valid) - 1U, "theme", "graphite") &&
        parse_decimal_value(config_value(valid, sizeof(valid) - 1U, "radius"), parsed) && parsed == 12U &&
        !config_value_equal(unrelated, sizeof(unrelated) - 1U, "theme", "graphite") &&
        !config_value(duplicate, sizeof(duplicate) - 1U, "theme") &&
        !parse_decimal_value(config_value(overflow, sizeof(overflow) - 1U, "radius"), parsed);
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
        0U, rgb(104, 146, 255), rgb(151, 122, 255), rgb(68, 191, 184), rgb(88, 188, 132), rgb(229, 126, 118)
    };
    return slot == 0U || slot >= accent_palette_size ? runtime_base_accent : colors[slot];
}

void apply_accent_personalization() {
    current_theme.accent = accent_palette_color(runtime_accent_slot);
    current_theme.accent_soft = mix(current_theme.accent, current_theme.surface, 2U, 3U);
    current_theme.glow_primary = mix(current_theme.accent, current_theme.canvas_top, 1U, 3U);
}


void apply_accessibility_personalization();

__attribute__((noinline)) uint32_t minimum_surface_contrast(const Theme& theme, uint32_t foreground) {
    uint32_t minimum = contrast_ratio_x100(foreground, theme.surface);
    const uint32_t high = contrast_ratio_x100(foreground, theme.surface_high);
    const uint32_t low = contrast_ratio_x100(foreground, theme.surface_low);
    if (high < minimum) minimum = high;
    if (low < minimum) minimum = low;
    return minimum;
}

__attribute__((noinline)) uint32_t role_contrast(const Theme& theme, uint32_t color, bool focus_role) {
    uint32_t minimum = minimum_surface_contrast(theme, color);
    if (focus_role) {
        const uint32_t soft = mix(color, theme.surface, 2U, 3U);
        const uint32_t focused = contrast_ratio_x100(color, soft);
        if (focused < minimum) minimum = focused;
    }
    return minimum;
}

__attribute__((noinline)) uint32_t best_role_target(const Theme& theme, bool focus_role) {
    const uint32_t light = rgb(255,255,255);
    const uint32_t dark = rgb(0,0,0);
    return role_contrast(theme, light, focus_role) >= role_contrast(theme, dark, focus_role) ? light : dark;
}

__attribute__((noinline)) void normalize_theme_contrast() {
    if (minimum_surface_contrast(current_theme, current_theme.text) < 450U)
        current_theme.text = best_role_target(current_theme, false);
    if (minimum_surface_contrast(current_theme, current_theme.text_muted) < 450U) {
        const uint32_t candidate = mix(current_theme.text_muted, current_theme.text, 1U, 2U);
        current_theme.text_muted = minimum_surface_contrast(current_theme, candidate) >= 450U
            ? candidate : current_theme.text;
    }
    if (minimum_surface_contrast(current_theme, current_theme.border) < 300U) {
        const uint32_t candidate = mix(current_theme.border, current_theme.text, 1U, 2U);
        current_theme.border = minimum_surface_contrast(current_theme, candidate) >= 300U
            ? candidate : current_theme.text;
    }
    if (role_contrast(current_theme, current_theme.accent, true) < 300U) {
        const uint32_t target = best_role_target(current_theme, true);
        const uint32_t candidate = mix(current_theme.accent, target, 1U, 2U);
        current_theme.accent = role_contrast(current_theme, candidate, true) >= 300U
            ? candidate : target;
    }
    current_theme.accent_soft = mix(current_theme.accent, current_theme.surface, 2U, 3U);
}

__attribute__((noinline)) bool current_theme_contrast_ok() {
    return minimum_surface_contrast(current_theme, current_theme.text) >= 450U &&
        minimum_surface_contrast(current_theme, current_theme.text_muted) >= 450U &&
        minimum_surface_contrast(current_theme, current_theme.border) >= 300U &&
        role_contrast(current_theme, current_theme.accent, true) >= 300U;
}

bool theme_contrast_contract_ok() {
    normalize_theme_contrast();
    return current_theme_contrast_ok();
}

void apply_accessibility_personalization() {
    if (runtime_high_contrast) {
        const uint32_t accessible_accent = current_theme.accent;
        current_theme.canvas_top = rgb(0,0,0);
        current_theme.canvas_bottom = rgb(0,0,0);
        current_theme.glow_primary = rgb(0,0,0);
        current_theme.glow_secondary = rgb(0,0,0);
        current_theme.surface = rgb(8,8,10);
        current_theme.surface_high = rgb(28,28,32);
        current_theme.surface_low = rgb(0,0,0);
        current_theme.border = rgb(244,244,248);
        current_theme.text = rgb(255,255,255);
        current_theme.text_muted = rgb(218,218,224);
        current_theme.accent = accessible_accent;
        current_theme.accent_soft = mix(accessible_accent, rgb(0,0,0), 3U, 5U);
        current_theme.terminal = rgb(0,0,0);
    }
    normalize_theme_contrast();
}

bool apply_config_text(const char* text, uint32_t size, bool& custom_colors) {
    if (!text || !size) return false;
    bool recognized = false;
    if (config_value_equal(text, size, "theme", "graphite")) { desktop_theme = "graphite"; recognized = true; }
    else if (config_value_equal(text, size, "theme", "amber")) { desktop_theme = "amber"; recognized = true; }
    else if (config_value_equal(text, size, "theme", "midnight") || config_value_equal(text, size, "theme", "zen-dark")) { desktop_theme = "midnight"; recognized = true; }

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
    if (config_value_equal(text, size, "wallpaper", "calm")) { runtime_wallpaper_style = 1U; recognized = true; }
    else if (config_value_equal(text, size, "wallpaper", "plain")) { runtime_wallpaper_style = 2U; recognized = true; }
    else if (config_value_equal(text, size, "wallpaper", "flow")) { runtime_wallpaper_style = 0U; recognized = true; }

    if (parse_decimal_value(config_value(text, size, "radius"), parsed) && parsed >= 6U && parsed <= 20U) {
        desktop_radius = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (parse_decimal_value(config_value(text, size, "opacity"), parsed) && parsed >= 176U && parsed <= 255U) {
        desktop_opacity = static_cast<uint8_t>(parsed); recognized = true;
    }
    if (parse_decimal_value(config_value(text, size, "animations"), parsed) && parsed <= 1U) {
        desktop_animations = parsed != 0U; recognized = true;
    }
    if (config_value_equal(text, size, "cursor", "block")) { terminal_cursor = "block"; recognized = true; }
    else if (config_value_equal(text, size, "cursor", "underline")) { terminal_cursor = "underline"; recognized = true; }
    else if (config_value_equal(text, size, "cursor", "beam")) { terminal_cursor = "beam"; recognized = true; }
    if (config_value_equal(text, size, "density", "compact")) { runtime_compact_density = true; recognized = true; }
    else if (config_value_equal(text, size, "density", "comfortable")) { runtime_compact_density = false; recognized = true; }
    if (config_value_equal(text, size, "taskbar", "left")) { runtime_taskbar_left = true; recognized = true; }
    else if (config_value_equal(text, size, "taskbar", "center")) { runtime_taskbar_left = false; recognized = true; }
    if (config_value_equal(text, size, "contrast", "high")) { runtime_high_contrast = true; recognized = true; }
    else if (config_value_equal(text, size, "contrast", "normal")) { runtime_high_contrast = false; recognized = true; }
    if (config_value_equal(text, size, "pointer", "large")) { runtime_large_pointer = true; recognized = true; }
    else if (config_value_equal(text, size, "pointer", "normal")) { runtime_large_pointer = false; recognized = true; }
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
        current_theme.accent_soft = mix(desktop_accent, desktop_surface, 2U, 3U);
    }
    runtime_base_accent = current_theme.accent;
    apply_accent_personalization();
    apply_accessibility_personalization();
}

void select_runtime_theme(uint8_t id) {
    apply_runtime_theme(id);
    desktop_theme = runtime_theme == 1U ? "graphite" : (runtime_theme == 2U ? "amber" : "midnight");
    runtime_base_accent = current_theme.accent;
    apply_accent_personalization();
    apply_accessibility_personalization();
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
        !append_config_text(text, sizeof(text), length, runtime_taskbar_left ? "left" : "center") ||
        !append_config_text(text, sizeof(text), length, "\ncontrast=") ||
        !append_config_text(text, sizeof(text), length, runtime_high_contrast ? "high" : "normal") ||
        !append_config_text(text, sizeof(text), length, "\npointer=") ||
        !append_config_text(text, sizeof(text), length, runtime_large_pointer ? "large\n" : "normal\n")) return false;
    return storage::write_file("/data/config/ui.cfg", reinterpret_cast<const uint8_t*>(text), length, false);
}

uint32_t focus_text_color(bool focused) {
    return focused ? current_theme.text : current_theme.text_muted;
}

constexpr int32_t focus_separator_thickness = 1;
constexpr int32_t focus_primary_thickness = 2;

void draw_focus_outline(int32_t x, int32_t y, int32_t outline_width, int32_t outline_height, uint32_t color) {
    if (outline_width <= 6 || outline_height <= 6) return;
    int32_t radius = (outline_width < outline_height ? outline_width : outline_height) / 4;
    if (radius < 4) radius = 4;
    if (radius > 10) radius = 10;
    const uint32_t light = rgb(255,255,255);
    const uint32_t dark = rgb(0,0,0);
    const uint32_t separator = contrast_ratio_x100(light, color) >= contrast_ratio_x100(dark, color) ? light : dark;
    draw_round_border(x, y, outline_width, outline_height, radius, separator);
    draw_round_border(x + 1, y + 1, outline_width - 2, outline_height - 2,
                      radius > 0 ? radius - 1 : 0, color);
    draw_round_border(x + 2, y + 2, outline_width - 4, outline_height - 4,
                      radius > 1 ? radius - 2 : 0, color);
}

bool focus_visual_contract_ok() {
    return focus_separator_thickness == 1 && focus_primary_thickness >= 2 &&
        contrast_ratio_x100(current_theme.accent, current_theme.accent_soft) >= 300U &&
        focus_text_color(true) == current_theme.text &&
        focus_text_color(false) == current_theme.text_muted;
}

void draw_wallpaper() {
    vertical_gradient(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height),
                      current_theme.canvas_top, current_theme.canvas_bottom);
    if (runtime_wallpaper_style == 2U) return;
    const uint8_t primary_alpha = runtime_wallpaper_style == 1U ? 4U : 6U;
    const uint8_t secondary_alpha = runtime_wallpaper_style == 1U ? 3U : 5U;
    fill_circle_alpha(110, 84, 226, current_theme.glow_primary, primary_alpha);
    fill_circle_alpha(780, 430, 272, current_theme.glow_secondary, secondary_alpha);
}

void draw_topbar() {}

enum class ShellIcon : uint8_t {
    start, terminal, files, settings, apps, display, packages, security,
    theme, motion, contrast, density, taskbar, count
};

constexpr int32_t shell_icon_size = 24;
constexpr int32_t shell_icon_stroke = 2;

void draw_icon_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    const int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        fill_rect(x0, y0, shell_icon_stroke, shell_icon_stroke, color);
        if (x0 == x1 && y0 == y1) break;
        const int32_t twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void draw_icon_round_outline(int32_t x, int32_t y, int32_t icon_width, int32_t icon_height,
                             int32_t radius, uint32_t color) {
    draw_round_border(x, y, icon_width, icon_height, radius, color);
    if (icon_width > 2 && icon_height > 2)
        draw_round_border(x + 1, y + 1, icon_width - 2, icon_height - 2,
                          radius > 1 ? radius - 1 : 0, color);
}

void draw_icon_circle_outline(int32_t center_x, int32_t center_y, int32_t radius, uint32_t color) {
    const int32_t outer = radius * radius;
    const int32_t inner_radius = radius > shell_icon_stroke ? radius - shell_icon_stroke : 0;
    const int32_t inner = inner_radius * inner_radius;
    for (int32_t row = -radius; row <= radius; ++row) {
        for (int32_t column = -radius; column <= radius; ++column) {
            const int32_t distance = column * column + row * row;
            if (distance <= outer && distance >= inner)
                fill_rect(center_x + column, center_y + row, 1, 1, color);
        }
    }
}

void fill_icon_half_circle(int32_t center_x, int32_t center_y, int32_t radius, uint32_t color) {
    const int32_t limit = radius * radius;
    for (int32_t row = -radius; row <= radius; ++row)
        for (int32_t column = -radius; column <= 0; ++column)
            if (column * column + row * row <= limit)
                fill_rect(center_x + column, center_y + row, 1, 1, color);
}

void draw_shell_icon(ShellIcon icon, int32_t x, int32_t y, uint32_t color) {
    if (icon == ShellIcon::start) {
        draw_icon_round_outline(x + 2, y + 2, 8, 8, 2, color);
        draw_icon_round_outline(x + 14, y + 2, 8, 8, 2, color);
        draw_icon_round_outline(x + 2, y + 14, 8, 8, 2, color);
        draw_icon_round_outline(x + 14, y + 14, 8, 8, 2, color);
        return;
    }
    if (icon == ShellIcon::terminal) {
        draw_icon_round_outline(x + 2, y + 4, 20, 16, 4, color);
        draw_icon_line(x + 6, y + 9, x + 9, y + 12, color);
        draw_icon_line(x + 9, y + 12, x + 6, y + 15, color);
        draw_icon_line(x + 12, y + 16, x + 17, y + 16, color);
        return;
    }
    if (icon == ShellIcon::files) {
        draw_icon_line(x + 3, y + 8, x + 8, y + 8, color);
        draw_icon_line(x + 8, y + 8, x + 11, y + 5, color);
        draw_icon_line(x + 11, y + 5, x + 20, y + 5, color);
        draw_icon_line(x + 20, y + 5, x + 21, y + 19, color);
        draw_icon_line(x + 21, y + 19, x + 3, y + 19, color);
        draw_icon_line(x + 3, y + 19, x + 3, y + 8, color);
        return;
    }
    if (icon == ShellIcon::settings) {
        draw_icon_line(x + 3, y + 5, x + 21, y + 5, color);
        draw_icon_line(x + 3, y + 12, x + 21, y + 12, color);
        draw_icon_line(x + 3, y + 19, x + 21, y + 19, color);
        fill_circle_alpha(x + 8, y + 6, 3, color, 255);
        fill_circle_alpha(x + 16, y + 13, 3, color, 255);
        fill_circle_alpha(x + 11, y + 20, 3, color, 255);
        return;
    }
    if (icon == ShellIcon::apps) {
        fill_circle_alpha(x + 7, y + 7, 3, color, 255);
        fill_circle_alpha(x + 17, y + 7, 3, color, 255);
        fill_circle_alpha(x + 7, y + 17, 3, color, 255);
        fill_circle_alpha(x + 17, y + 17, 3, color, 255);
        return;
    }
    if (icon == ShellIcon::display) {
        draw_icon_round_outline(x + 2, y + 3, 20, 15, 3, color);
        draw_icon_line(x + 12, y + 18, x + 12, y + 21, color);
        draw_icon_line(x + 7, y + 21, x + 17, y + 21, color);
        return;
    }
    if (icon == ShellIcon::packages) {
        draw_icon_round_outline(x + 4, y + 4, 16, 17, 3, color);
        draw_icon_line(x + 4, y + 9, x + 12, y + 13, color);
        draw_icon_line(x + 20, y + 9, x + 12, y + 13, color);
        draw_icon_line(x + 12, y + 13, x + 12, y + 21, color);
        return;
    }
    if (icon == ShellIcon::security) {
        draw_icon_line(x + 12, y + 2, x + 20, y + 5, color);
        draw_icon_line(x + 20, y + 5, x + 19, y + 14, color);
        draw_icon_line(x + 19, y + 14, x + 12, y + 22, color);
        draw_icon_line(x + 12, y + 22, x + 5, y + 14, color);
        draw_icon_line(x + 5, y + 14, x + 4, y + 5, color);
        draw_icon_line(x + 4, y + 5, x + 12, y + 2, color);
        draw_icon_line(x + 8, y + 12, x + 11, y + 15, color);
        draw_icon_line(x + 11, y + 15, x + 16, y + 9, color);
        return;
    }
    if (icon == ShellIcon::theme) {
        draw_icon_circle_outline(x + 12, y + 12, 5, color);
        draw_icon_line(x + 12, y + 1, x + 12, y + 4, color);
        draw_icon_line(x + 12, y + 20, x + 12, y + 22, color);
        draw_icon_line(x + 1, y + 12, x + 4, y + 12, color);
        draw_icon_line(x + 20, y + 12, x + 22, y + 12, color);
        draw_icon_line(x + 4, y + 4, x + 6, y + 6, color);
        draw_icon_line(x + 18, y + 18, x + 20, y + 20, color);
        draw_icon_line(x + 18, y + 6, x + 20, y + 4, color);
        draw_icon_line(x + 4, y + 20, x + 6, y + 18, color);
        return;
    }
    if (icon == ShellIcon::motion) {
        draw_icon_line(x + 7, y + 4, x + 19, y + 12, color);
        draw_icon_line(x + 19, y + 12, x + 7, y + 20, color);
        draw_icon_line(x + 7, y + 20, x + 7, y + 4, color);
        return;
    }
    if (icon == ShellIcon::contrast) {
        fill_icon_half_circle(x + 12, y + 12, 8, color);
        draw_icon_circle_outline(x + 12, y + 12, 9, color);
        draw_icon_line(x + 12, y + 3, x + 12, y + 20, color);
        return;
    }
    if (icon == ShellIcon::density) {
        draw_icon_line(x + 4, y + 6, x + 20, y + 6, color);
        draw_icon_line(x + 4, y + 12, x + 20, y + 12, color);
        draw_icon_line(x + 4, y + 18, x + 20, y + 18, color);
        return;
    }
    if (icon == ShellIcon::taskbar) {
        draw_icon_round_outline(x + 3, y + 4, 18, 16, 3, color);
        draw_icon_line(x + 4, y + 16, x + 19, y + 16, color);
        return;
    }
}

void draw_window_chrome(const char* title, const char* subtitle) {
    (void)subtitle;
    const int32_t radius = static_cast<int32_t>(desktop_radius);
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 3U);
    draw_shadow(window_x, window_y, window_width, window_height, radius);
    fill_round_rect_alpha(window_x, window_y, window_width, window_height, radius,
                          current_theme.surface, desktop_opacity);
    draw_round_border(window_x, window_y, window_width, window_height, radius, shell_border);
    fill_round_rect(window_x + 1, window_y + 1, window_width - 2, titlebar_height + 3,
                    radius > 1 ? radius - 1 : 0, current_theme.surface_low);
    fill_rect(window_x + 1, window_y + titlebar_height + 1, window_width - 2, 1, shell_border);
    draw_text_strong_ellipsis(window_x + 20, window_y + 16, window_width - 40, title, current_theme.text, 1);
}

constexpr uint32_t vga_colors[16] = {
    rgb(10,12,20), rgb(66,96,180), rgb(72,166,118), rgb(74,166,176),
    rgb(190,72,82), rgb(157,91,193), rgb(201,132,57), rgb(200,207,220),
    rgb(93,101,120), rgb(111,143,232), rgb(113,217,168), rgb(102,203,220),
    rgb(239,125,146), rgb(202,151,238), rgb(242,198,109), rgb(244,246,255)
};
