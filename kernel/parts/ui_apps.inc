constexpr int32_t terminal_panel_x_offset = 8;
constexpr int32_t terminal_panel_y_offset = 52;
constexpr int32_t terminal_panel_width = 496;
constexpr int32_t terminal_panel_height = 336;
constexpr int32_t terminal_line_height = 10;

constexpr int32_t settings_sidebar_x_offset = 16;
constexpr int32_t settings_sidebar_width = 120;
constexpr int32_t settings_page_y_offsets[4] = {96, 134, 172, 210};
constexpr int32_t settings_page_height = 32;
constexpr int32_t settings_content_x_offset = 152;
constexpr int32_t settings_content_width = 336;
constexpr int32_t settings_theme_width = 96;
constexpr int32_t settings_theme_height = 52;
constexpr int32_t settings_theme_x_offsets[3] = {152, 256, 360};
constexpr int32_t settings_theme_y_offset = 106;
constexpr int32_t settings_display_y_offset = 174;
constexpr int32_t settings_display_height = 50;
constexpr int32_t settings_motion_y_offset = 238;
constexpr int32_t settings_cursor_y_offset = 282;
constexpr int32_t settings_accent_y_offset = 116;
constexpr int32_t settings_wallpaper_y_offset = 174;
constexpr int32_t settings_radius_y_offset = 218;
constexpr int32_t settings_opacity_y_offset = 260;
constexpr int32_t settings_density_y_offset = 302;
constexpr int32_t settings_taskbar_y_offset = 344;
constexpr int32_t settings_access_contrast_y_offset = 96;
constexpr int32_t settings_access_pointer_y_offset = 148;
constexpr int32_t settings_access_motion_y_offset = 200;
constexpr int32_t settings_reset_y_offset = 314;

constexpr int32_t launcher_panel_x = 208;
constexpr int32_t launcher_panel_y = 18;
constexpr int32_t launcher_panel_width = 480;
constexpr int32_t launcher_panel_height = 336;
constexpr int32_t launcher_search_x = launcher_panel_x + 16;
constexpr int32_t launcher_search_y = launcher_panel_y + 16;
constexpr int32_t launcher_search_width = launcher_panel_width - 32;
constexpr int32_t launcher_search_height = 40;
constexpr int32_t launcher_card_width = 448;
constexpr int32_t launcher_card_height = 40;

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

void render_terminal_home() {
    const int32_t panel_x = window_x + terminal_panel_x_offset;
    const int32_t panel_y = window_y + terminal_panel_y_offset;
    fill_round_rect(panel_x, panel_y, terminal_panel_width, terminal_panel_height, 9, current_theme.terminal);
    draw_console_content_row(console::row, 0, panel_x + 20, panel_y + 28);
    draw_terminal_caret(panel_x + 20, panel_y + 28, console::row);
    fill_rect(panel_x + 20, panel_y + terminal_panel_height - 38, terminal_panel_width - 40, 1,
              mix(current_theme.border, current_theme.terminal, 1U, 2U));
    draw_text(panel_x + 20, panel_y + terminal_panel_height - 22, "Type help for commands", current_theme.text_muted, 1);
}

void render_terminal_cells() {
    if (terminal_home_visible()) { render_terminal_home(); return; }
    const int32_t panel_x = window_x + terminal_panel_x_offset;
    const int32_t panel_y = window_y + terminal_panel_y_offset;
    fill_round_rect(panel_x, panel_y, terminal_panel_width, terminal_panel_height, 9, current_theme.terminal);
    const int32_t origin_x = window_x + 16;
    const int32_t origin_y = window_y + 68;
    for (uint32_t row = console::content_top; row < console::content_bottom; ++row)
        draw_console_content_row(row, row - console::content_top, origin_x, origin_y);
    draw_terminal_caret(origin_x, origin_y, console::content_top);
}

void render_terminal_placeholder() {
    fill_round_rect(window_x + terminal_panel_x_offset, window_y + terminal_panel_y_offset,
                    terminal_panel_width, terminal_panel_height, 9, current_theme.terminal);
    draw_text(window_x + 24, window_y + 88, "Terminal", current_theme.text, 2);
    draw_text(window_x + 24, window_y + 116, "Starting local session", current_theme.text_muted, 1);
}

void render_terminal() {
    draw_window_chrome("Terminal", "Local shell");
    if (terminal_sync_started) render_terminal_cells(); else render_terminal_placeholder();
}

uint8_t settings_focus_count() {
    if (settings_page == 2U) return 3U;
    if (settings_page == 3U) return 1U;
    return 6U;
}

HoverTarget settings_page_target(uint8_t index) {
    static constexpr HoverTarget targets[settings_page_count] = {
        HoverTarget::settings_page_general, HoverTarget::settings_page_style,
        HoverTarget::settings_page_access, HoverTarget::settings_page_about
    };
    return index < settings_page_count ? targets[index] : HoverTarget::none;
}

void draw_settings_page_button(uint8_t index, const char* name, ShellIcon icon) {
    const int32_t x = window_x + settings_sidebar_x_offset;
    const int32_t y = window_y + settings_page_y_offsets[index];
    const bool selected = settings_page == index;
    const bool hovered = is_hovered(settings_page_target(index));
    if (selected) fill_round_rect(x, y, settings_sidebar_width, settings_page_height, 8, current_theme.accent_soft);
    else if (hovered) fill_round_rect(x, y, settings_sidebar_width, settings_page_height, 8, current_theme.surface_high);
    draw_shell_icon(icon, x + 10, y + 6, selected ? current_theme.accent : current_theme.text_muted);
    draw_text(x + 42, y + 14, name, selected ? current_theme.text : current_theme.text_muted, 1);
}

void draw_settings_sidebar() {
    draw_text(window_x + 20, window_y + 70, "Settings", current_theme.text, 1);
    draw_settings_page_button(0, "General", ShellIcon::settings);
    draw_settings_page_button(1, "Style", ShellIcon::apps);
    draw_settings_page_button(2, "Access", ShellIcon::display);
    draw_settings_page_button(3, "About", ShellIcon::start);
    fill_rect(window_x + 140, window_y + 58, 1, 322, current_theme.border);
}

void draw_settings_theme(uint32_t index, uint32_t color, const char* name, HoverTarget target) {
    const int32_t x = window_x + settings_theme_x_offsets[index];
    const int32_t y = window_y + settings_theme_y_offset;
    const bool selected = runtime_theme == index;
    const bool focused = settings_focus == index;
    uint32_t fill = current_theme.surface_low;
    if (selected || focused) fill = current_theme.accent_soft;
    else if (is_hovered(target)) fill = current_theme.surface_high;
    fill_round_rect(x, y, settings_theme_width, settings_theme_height, 9, fill);
    fill_round_rect(x + 12, y + 12, settings_theme_width - 24, 3, 1, color);
    draw_text(x + 12, y + 29, name, current_theme.text, 1);
    if (selected) fill_circle_alpha(x + settings_theme_width - 16, y + 32, 3, current_theme.accent, 255);
    if (focused) draw_focus_outline(x, y, settings_theme_width, settings_theme_height, current_theme.accent);
}

void draw_display_value(int32_t right, int32_t y) { char text[16]; format_display_mode(text); draw_text_right(right, y, text, current_theme.text, 1); }

void draw_settings_display() {
    const int32_t x = window_x + settings_content_x_offset;
    const int32_t y = window_y + settings_display_y_offset;
    const bool focused = settings_focus == 3U;
    const bool hovered = is_hovered(HoverTarget::settings_display);
    fill_round_rect(x, y, settings_content_width, settings_display_height, 9,
                    focused || hovered ? mix(current_theme.surface_high, current_theme.accent, 1U, 8U) : current_theme.surface_high);
    fill_round_rect(x + 10, y + 10, 30, 30, 7, current_theme.surface_low);
    draw_shell_icon(ShellIcon::display, x + 13, y + 13, current_theme.accent);
    draw_text(x + 50, y + 14, "Display", current_theme.text, 1);
    draw_text(x + 50, y + 32, "Left / Right", focus_text_color(focused), 1);
    draw_display_value(x + settings_content_width - 20, y + 22);
    if (focused) draw_focus_outline(x, y, settings_content_width, settings_display_height, current_theme.accent);
}

void draw_settings_row(int32_t y, const char* label, const char* value, uint8_t focus_index, HoverTarget target) {
    const int32_t x = window_x + settings_content_x_offset;
    const bool focused = settings_focus == focus_index;
    const bool hovered = is_hovered(target);
    uint32_t fill = current_theme.surface_low;
    if (focused) fill = current_theme.accent_soft;
    else if (hovered) fill = current_theme.surface_high;
    fill_round_rect(x, y - 10, settings_content_width, 36, 8, fill);
    draw_text_ellipsis(x + 12, y, settings_content_width - 128, label, focus_text_color(focused), 1);
    draw_text_right_ellipsis(x + settings_content_width - 12, y, 110, value, current_theme.text, 1);
    if (focused) draw_focus_outline(x, y - 10, settings_content_width, 36, current_theme.accent);
}

void render_settings_general() {
    draw_text(window_x + settings_content_x_offset, window_y + 68, "General", current_theme.text, 2);
    const uint32_t themes[3] = {rgb(125,140,255), rgb(132,148,188), rgb(222,155,84)};
    const char* names[3] = {"Dark", "Graphite", "Amber"};
    const HoverTarget targets[3] = {HoverTarget::theme_midnight, HoverTarget::theme_graphite, HoverTarget::theme_amber};
    for (uint32_t i = 0; i < 3U; ++i) draw_settings_theme(i, themes[i], names[i], targets[i]);
    draw_settings_display();
    draw_settings_row(window_y + settings_motion_y_offset, "Motion", desktop_animations ? "Smooth" : "Reduced", 4U, HoverTarget::settings_motion);
    draw_settings_row(window_y + settings_cursor_y_offset, "Terminal cursor", terminal_cursor, 5U, HoverTarget::settings_cursor);
    draw_text(window_x + settings_content_x_offset, window_y + 350, "Tab select   Arrows change", current_theme.text_muted, 1);
}

void draw_accent_swatches() {
    const int32_t x = window_x + settings_content_x_offset;
    const int32_t y = window_y + settings_accent_y_offset;
    const bool focused = settings_focus == 0U;
    const bool hovered = is_hovered(HoverTarget::settings_accent);
    fill_round_rect(x, y - 14, settings_content_width, 52, 9,
                    focused ? current_theme.accent_soft : (hovered ? current_theme.surface_high : current_theme.surface_low));
    draw_text(x + 12, y - 2, "Accent", focus_text_color(focused), 1);
    for (uint8_t i = 0; i < accent_palette_size; ++i) {
        const int32_t swatch_x = x + 112 + static_cast<int32_t>(i * 34U);
        const uint32_t color = i == 0U ? runtime_base_accent : accent_palette_color(i);
        fill_circle_alpha(swatch_x, y + 6, 9, color, 255);
        if (runtime_accent_slot == i) fill_circle_alpha(swatch_x, y + 6, 3, current_theme.text, 255);
    }
    if (focused) draw_focus_outline(x, y - 14, settings_content_width, 52, current_theme.accent);
}

void draw_uint_value_right(int32_t right, int32_t y, uint32_t value, uint32_t color) {
    char text[12]; char reverse[11]; uint32_t count = 0, length = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
    while (count) text[length++] = reverse[--count];
    text[length] = 0;
    draw_text_right(right, y, text, color, 1);
}

void render_settings_style() {
    draw_text(window_x + settings_content_x_offset, window_y + 68, "Personalize", current_theme.text, 2);
    draw_accent_swatches();
    draw_settings_row(window_y + settings_wallpaper_y_offset, "Wallpaper", wallpaper_name(), 1U, HoverTarget::settings_wallpaper);
    const int32_t right = window_x + settings_content_x_offset + settings_content_width - 12;
    const int32_t radius_y = window_y + settings_radius_y_offset;
    const bool radius_focus = settings_focus == 2U;
    uint32_t radius_fill = current_theme.surface_low;
    if (radius_focus) radius_fill = current_theme.accent_soft;
    else if (is_hovered(HoverTarget::settings_radius)) radius_fill = current_theme.surface_high;
    fill_round_rect(window_x + settings_content_x_offset, radius_y - 10, settings_content_width, 36, 8, radius_fill);
    draw_text(window_x + settings_content_x_offset + 12, radius_y, "Corner radius", focus_text_color(radius_focus), 1);
    draw_uint_value_right(right, radius_y, desktop_radius, current_theme.text);
    if (radius_focus) draw_focus_outline(window_x + settings_content_x_offset, radius_y - 10,
                                         settings_content_width, 36, current_theme.accent);
    const int32_t opacity_y = window_y + settings_opacity_y_offset;
    const bool opacity_focus = settings_focus == 3U;
    uint32_t opacity_fill = current_theme.surface_low;
    if (opacity_focus) opacity_fill = current_theme.accent_soft;
    else if (is_hovered(HoverTarget::settings_opacity)) opacity_fill = current_theme.surface_high;
    fill_round_rect(window_x + settings_content_x_offset, opacity_y - 10, settings_content_width, 36, 8, opacity_fill);
    draw_text(window_x + settings_content_x_offset + 12, opacity_y, "Panel opacity", focus_text_color(opacity_focus), 1);
    draw_uint_value_right(right, opacity_y, desktop_opacity, current_theme.text);
    if (opacity_focus) draw_focus_outline(window_x + settings_content_x_offset, opacity_y - 10,
                                          settings_content_width, 36, current_theme.accent);
    draw_settings_row(window_y + settings_density_y_offset, "Density", runtime_compact_density ? "Compact" : "Comfort", 4U, HoverTarget::settings_density);
    draw_settings_row(window_y + settings_taskbar_y_offset, "Taskbar", runtime_taskbar_left ? "Left" : "Center", 5U, HoverTarget::settings_taskbar);
}


void render_settings_access() {
    draw_text(window_x + settings_content_x_offset, window_y + 68, "Accessibility", current_theme.text, 2);
    draw_settings_row(window_y + settings_access_contrast_y_offset + 34, "High contrast", runtime_high_contrast ? "On" : "Off", 0U, HoverTarget::settings_contrast);
    draw_settings_row(window_y + settings_access_pointer_y_offset + 34, "Pointer size", runtime_large_pointer ? "Large" : "Normal", 1U, HoverTarget::settings_pointer);
    draw_settings_row(window_y + settings_access_motion_y_offset + 34, "Motion", desktop_animations ? "Smooth" : "Reduced", 2U, HoverTarget::settings_access_motion);
}

void render_settings_about() {
    draw_text(window_x + settings_content_x_offset, window_y + 68, "About", current_theme.text, 2);
    const int32_t x = window_x + settings_content_x_offset;
    fill_round_rect(x, window_y + 104, settings_content_width, 142, 10, current_theme.surface_low);
    draw_text(x + 16, window_y + 126, "Architecture", current_theme.text_muted, 1);
    draw_text_right(x + settings_content_width - 16, window_y + 126, "i686", current_theme.text, 1);
    draw_text(x + 16, window_y + 154, "Display", current_theme.text_muted, 1);
    draw_display_value(x + settings_content_width - 16, window_y + 154);
    draw_text(x + 16, window_y + 182, "Interface", current_theme.text_muted, 1);
    draw_text_right(x + settings_content_width - 16, window_y + 182, "896x504", current_theme.text, 1);
    draw_text(x + 16, window_y + 210, "Configuration", current_theme.text_muted, 1);
    draw_text_right(x + settings_content_width - 16, window_y + 210, "/data/config/ui.cfg", current_theme.text, 1);
    const int32_t reset_y = window_y + settings_reset_y_offset;
    const bool focused = settings_focus == 0U;
    fill_round_rect(x, reset_y, settings_content_width, 46, 9,
                    focused ? current_theme.accent_soft : current_theme.surface_low);
    draw_text(x + 16, reset_y + 18, "Reset personalization", current_theme.text, 1);
    if (focused) draw_focus_outline(x, reset_y, settings_content_width, 46, current_theme.accent);
}

void render_settings() {
    const char* section = settings_page == 0U ? "General" : (settings_page == 1U ? "Style" : (settings_page == 2U ? "Access" : "About"));
    draw_window_chrome("Settings", section);
    draw_settings_sidebar();
    if (settings_page == 0U) render_settings_general();
    else if (settings_page == 1U) render_settings_style();
    else if (settings_page == 2U) render_settings_access();
    else render_settings_about();
}

const char* launcher_names[launcher_item_count] = {"Terminal", "Files", "Settings", "Display", "Packages", "Security"};
bool launcher_filter_reported = false;
constexpr ShellIcon launcher_icons[launcher_item_count] = {
    ShellIcon::terminal, ShellIcon::files, ShellIcon::settings, ShellIcon::display, ShellIcon::apps, ShellIcon::settings
};

char launcher_lower(char value) { return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value; }

bool launcher_item_matches(uint8_t index) {
    if (index >= launcher_item_count || launcher_query_length == 0U) return index < launcher_item_count;
    const char* name = launcher_names[index];
    const uint32_t name_length = static_cast<uint32_t>(string_length(name));
    if (launcher_query_length > name_length) return false;
    for (uint32_t start = 0; start + launcher_query_length <= name_length; ++start) {
        bool matches = true;
        for (uint32_t at = 0; at < launcher_query_length; ++at) {
            if (launcher_lower(name[start + at]) != launcher_lower(launcher_query[at])) { matches = false; break; }
        }
        if (matches) return true;
    }
    return false;
}

uint8_t launcher_first_match() {
    for (uint8_t i = 0; i < launcher_item_count; ++i) if (launcher_item_matches(i)) return i;
    return 0U;
}

uint8_t launcher_match_count() {
    uint8_t count = 0U;
    for (uint8_t i = 0; i < launcher_item_count; ++i) if (launcher_item_matches(i)) ++count;
    return count;
}

void launcher_report_filter() {
    if (launcher_filter_reported || !string_equal(launcher_query, "set")) return;
    if (launcher_match_count() != 1U || !launcher_item_matches(2U)) return;
    serial::line("UI_START_FILTER_OK");
    launcher_filter_reported = true;
}

void launcher_move_focus(bool forward) {
    for (uint8_t offset = 1U; offset <= launcher_item_count; ++offset) {
        const uint8_t candidate = forward
            ? static_cast<uint8_t>((launcher_focus + offset) % launcher_item_count)
            : static_cast<uint8_t>((launcher_focus + launcher_item_count - (offset % launcher_item_count)) % launcher_item_count);
        if (launcher_item_matches(candidate)) { launcher_focus = candidate; return; }
    }
}

void launcher_append(char value) {
    if (launcher_query_length + 1U >= sizeof(launcher_query)) return;
    launcher_query[launcher_query_length++] = value;
    launcher_query[launcher_query_length] = 0;
    launcher_focus = launcher_first_match();
    serial::write("UI_START_QUERY_CHAR "); serial::put(value); serial::write("\r\n");
    launcher_report_filter();
}

void launcher_backspace() {
    if (launcher_query_length) launcher_query[--launcher_query_length] = 0;
    launcher_focus = launcher_first_match();
}

uint8_t launcher_visible_rank(uint8_t index) {
    uint8_t rank = 0U;
    for (uint8_t i = 0U; i < index; ++i) if (launcher_item_matches(i)) ++rank;
    return rank;
}
int32_t launcher_card_x(uint8_t) { return launcher_panel_x + 20; }
int32_t launcher_card_y(uint8_t index) { return launcher_panel_y + 66 + static_cast<int32_t>(launcher_visible_rank(index)) * 42; }
HoverTarget launcher_target(uint8_t index) {
    static constexpr HoverTarget targets[launcher_item_count] = {
        HoverTarget::launcher_terminal, HoverTarget::launcher_files, HoverTarget::launcher_settings,
        HoverTarget::launcher_display, HoverTarget::launcher_packages, HoverTarget::launcher_security
    };
    return index < launcher_item_count ? targets[index] : HoverTarget::none;
}

constexpr int32_t launcher_panel_height_for_count(uint8_t count) {
    return 84 + static_cast<int32_t>(count) * 42;
}

int32_t launcher_panel_height_current() { return launcher_panel_height_for_count(launcher_match_count()); }

void draw_launcher_card(uint8_t index) {
    if (!launcher_item_matches(index)) return;
    const int32_t x = launcher_card_x(index);
    const int32_t y = launcher_card_y(index) + ui_scene_offset_y;
    const bool focused = launcher_focus == index;
    const bool hovered = is_hovered(launcher_target(index));
    uint32_t fill = current_theme.surface_low;
    if (focused) fill = current_theme.accent_soft;
    else if (hovered) fill = current_theme.surface_high;
    fill_round_rect(x, y, launcher_card_width, launcher_card_height, 9, fill);
    draw_shell_icon(launcher_icons[index], x + 13, y + 9,
                    focused ? current_theme.accent : current_theme.text_muted);
    draw_text(x + 48, y + 16, launcher_names[index], current_theme.text, 1);
    if (focused) draw_focus_outline(x, y, launcher_card_width, launcher_card_height, current_theme.accent);
}

void render_launcher() {
    const int32_t y = launcher_panel_y + ui_scene_offset_y;
    const int32_t panel_height = launcher_panel_height_current();
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 3U);
    draw_shadow(launcher_panel_x, y, launcher_panel_width, panel_height, 16);
    fill_round_rect_alpha(launcher_panel_x, y, launcher_panel_width, panel_height, 16,
                          current_theme.surface, desktop_opacity);
    draw_round_border(launcher_panel_x, y, launcher_panel_width, panel_height, 16, shell_border);
    fill_round_rect(launcher_search_x, launcher_search_y + ui_scene_offset_y,
                    launcher_search_width, launcher_search_height, 10, current_theme.surface_low);
    if (launcher_query_length)
        draw_text(launcher_search_x + 16, launcher_search_y + 17 + ui_scene_offset_y,
                  launcher_query, current_theme.text, 1);
    else
        draw_text(launcher_search_x + 16, launcher_search_y + 17 + ui_scene_offset_y,
                  "Search", current_theme.text_muted, 1);
    for (uint8_t i = 0; i < launcher_item_count; ++i) draw_launcher_card(i);
}
