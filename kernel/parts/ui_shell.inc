constexpr int32_t taskbar_margin = 14;
constexpr int32_t taskbar_main_width_comfortable = 204;
constexpr int32_t taskbar_main_width_compact = 172;
constexpr int32_t taskbar_tray_width = 150;
constexpr int32_t quick_panel_width = 270;
constexpr int32_t quick_panel_height = 252;
constexpr int32_t quick_tile_width = 112;
constexpr int32_t quick_tile_height = 56;

int32_t taskbar_height() { return runtime_compact_density ? 38 : 46; }
int32_t taskbar_y() { return static_cast<int32_t>(height) - taskbar_margin - taskbar_height(); }
int32_t taskbar_main_width() { return runtime_compact_density ? taskbar_main_width_compact : taskbar_main_width_comfortable; }
int32_t taskbar_main_x() {
    return runtime_taskbar_left ? taskbar_margin : (static_cast<int32_t>(width) - taskbar_main_width()) / 2;
}
int32_t taskbar_tray_x() { return static_cast<int32_t>(width) - taskbar_margin - taskbar_tray_width; }
int32_t taskbar_button_size() { return runtime_compact_density ? 30 : 38; }
int32_t taskbar_button_gap() { return runtime_compact_density ? 8 : 10; }
int32_t taskbar_button_x(uint8_t index) {
    return taskbar_main_x() + 8 + static_cast<int32_t>(index) * (taskbar_button_size() + taskbar_button_gap());
}

uint8_t cmos_read(uint8_t index) {
    out8(0x70, index);
    return in8(0x71);
}

uint8_t bcd_to_binary(uint8_t value) { return static_cast<uint8_t>((value & 0x0FU) + ((value >> 4U) * 10U)); }

bool rtc_read_time(uint8_t& hour, uint8_t& minute) {
    for (uint32_t retry = 0; retry < 8U; ++retry) {
        if (cmos_read(0x0A) & 0x80U) continue;
        const uint8_t first_minute = cmos_read(0x02);
        uint8_t first_hour = cmos_read(0x04);
        const uint8_t status_b = cmos_read(0x0B);
        if (cmos_read(0x0A) & 0x80U) continue;
        if (first_minute != cmos_read(0x02) || first_hour != cmos_read(0x04)) continue;
        const bool pm = (first_hour & 0x80U) != 0;
        first_hour = static_cast<uint8_t>(first_hour & 0x7FU);
        minute = first_minute;
        hour = first_hour;
        if ((status_b & 0x04U) == 0U) {
            minute = bcd_to_binary(minute);
            hour = bcd_to_binary(hour);
        }
        if ((status_b & 0x02U) == 0U) {
            if (pm && hour < 12U) hour = static_cast<uint8_t>(hour + 12U);
            else if (!pm && hour == 12U) hour = 0U;
        }
        return hour < 24U && minute < 60U;
    }
    return false;
}

void draw_two_digits(int32_t x, int32_t y, uint8_t value, uint32_t color) {
    draw_char(x, y, static_cast<char>('0' + (value / 10U)), color, 1);
    draw_char(x + 6, y, static_cast<char>('0' + (value % 10U)), color, 1);
}

void format_display_mode(char output[16]) {
    const DisplayMode mode = display_modes[active_display_mode];
    char reverse[6]; uint32_t count = 0, length = 0, value = mode.width;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
    while (count) output[length++] = reverse[--count];
    output[length++] = 'X'; count = 0; value = mode.height;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
    while (count) output[length++] = reverse[--count];
    output[length] = 0;
}

void draw_taskbar_button(uint8_t index, ShellIcon icon, bool selected, HoverTarget target) {
    const int32_t size = taskbar_button_size();
    const int32_t x = taskbar_button_x(index);
    const int32_t y = taskbar_y() + (taskbar_height() - size) / 2;
    uint32_t fill = current_theme.surface_high;
    if (selected) fill = current_theme.accent_soft;
    else if (is_hovered(target)) fill = mix(current_theme.surface_high, current_theme.accent, 1U, 7U);
    fill_round_rect(x, y, size, size, runtime_compact_density ? 8 : 10, fill);
    const int32_t icon_x = x + (size - 23) / 2;
    const int32_t icon_y = y + (size - 23) / 2;
    draw_shell_icon(icon, icon_x, icon_y, selected ? current_theme.accent : current_theme.text_muted);
    if (selected) fill_round_rect(x + size / 2 - 7, taskbar_y() + taskbar_height() - 4, 14, 2, 1, current_theme.accent);
}

void draw_taskbar() {
    const int32_t radius = runtime_compact_density ? 10 : 13;
    fill_round_rect_alpha(taskbar_main_x(), taskbar_y(), taskbar_main_width(), taskbar_height(), radius,
                          current_theme.surface_low, desktop_opacity);
    draw_taskbar_button(0, ShellIcon::start, active_view == View::launcher, HoverTarget::taskbar_start);
    draw_taskbar_button(1, ShellIcon::terminal, active_view == View::terminal, HoverTarget::taskbar_terminal);
    draw_taskbar_button(2, ShellIcon::files, active_view == View::files, HoverTarget::taskbar_files);
    draw_taskbar_button(3, ShellIcon::settings, active_view == View::settings, HoverTarget::taskbar_settings);

    uint32_t tray_fill = current_theme.surface_low;
    if (quick_settings_open) tray_fill = current_theme.accent_soft;
    else if (is_hovered(HoverTarget::taskbar_tray)) tray_fill = current_theme.surface_high;
    fill_round_rect_alpha(taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height(), radius,
                          tray_fill, desktop_opacity);
    fill_circle_alpha(taskbar_tray_x() + 17, taskbar_y() + taskbar_height() / 2, 3, current_theme.success, 255);
    draw_text(taskbar_tray_x() + 28, taskbar_y() + (runtime_compact_density ? 15 : 19), "LOCAL", current_theme.text_muted, 1);
    uint8_t hour = 0, minute = 0;
    if (rtc_read_time(hour, minute)) {
        const int32_t clock_x = taskbar_tray_x() + taskbar_tray_width - 44;
        const int32_t clock_y = taskbar_y() + (runtime_compact_density ? 15 : 19);
        draw_two_digits(clock_x, clock_y, hour, current_theme.text);
        draw_char(clock_x + 13, clock_y, ':', current_theme.text_muted, 1);
        draw_two_digits(clock_x + 19, clock_y, minute, current_theme.text);
    } else draw_text_right(taskbar_tray_x() + taskbar_tray_width - 12, taskbar_y() + 19, "RTC", current_theme.warning, 1);
}

int32_t quick_panel_x() { return static_cast<int32_t>(width) - taskbar_margin - quick_panel_width; }
int32_t quick_panel_y() { return taskbar_y() - quick_panel_height - 10; }
int32_t quick_tile_x(uint8_t index) { return quick_panel_x() + 16 + static_cast<int32_t>(index % 2U) * 126; }
int32_t quick_tile_y(uint8_t index) { return quick_panel_y() + 54 + static_cast<int32_t>(index / 2U) * 64; }

HoverTarget quick_target(uint8_t index) {
    static constexpr HoverTarget targets[quick_settings_count] = {
        HoverTarget::quick_theme, HoverTarget::quick_display, HoverTarget::quick_motion,
        HoverTarget::quick_cursor, HoverTarget::quick_density, HoverTarget::quick_taskbar
    };
    return index < quick_settings_count ? targets[index] : HoverTarget::none;
}

const char* quick_label(uint8_t index) {
    static constexpr const char* labels[quick_settings_count] = {"THEME", "DISPLAY", "MOTION", "CURSOR", "DENSITY", "TASKBAR"};
    return index < quick_settings_count ? labels[index] : "";
}

const char* wallpaper_name() { return runtime_wallpaper_style == 1U ? "CALM" : (runtime_wallpaper_style == 2U ? "PLAIN" : "FLOW"); }
const char* theme_name() { return runtime_theme == 1U ? "GRAPHITE" : (runtime_theme == 2U ? "AMBER" : "MIDNIGHT"); }

void draw_quick_tile(uint8_t index) {
    const int32_t x = quick_tile_x(index), y = quick_tile_y(index);
    const bool focused = quick_settings_focus == index;
    const bool hovered = is_hovered(quick_target(index));
    uint32_t fill = current_theme.surface_high;
    if (focused) fill = current_theme.accent_soft;
    else if (hovered) fill = mix(current_theme.surface_high, current_theme.accent, 1U, 8U);
    fill_round_rect(x, y, quick_tile_width, quick_tile_height, 10, fill);
    fill_circle_alpha(x + 18, y + 18, 5, focused ? current_theme.accent : current_theme.text_muted, 255);
    draw_text(x + 32, y + 14, quick_label(index), current_theme.text, 1);
    if (index == 0U) draw_text(x + 12, y + 38, theme_name(), current_theme.text_muted, 1);
    else if (index == 1U) { char mode[16]; format_display_mode(mode); draw_text(x + 12, y + 38, mode, current_theme.text_muted, 1); }
    else if (index == 2U) draw_text(x + 12, y + 38, desktop_animations ? "SMOOTH" : "REDUCED", current_theme.text_muted, 1);
    else if (index == 3U) draw_text(x + 12, y + 38, terminal_cursor, current_theme.text_muted, 1);
    else if (index == 4U) draw_text(x + 12, y + 38, runtime_compact_density ? "COMPACT" : "COMFORT", current_theme.text_muted, 1);
    else draw_text(x + 12, y + 38, runtime_taskbar_left ? "LEFT" : "CENTER", current_theme.text_muted, 1);
}

void draw_quick_settings() {
    if (!quick_settings_open) return;
    draw_shadow(quick_panel_x(), quick_panel_y(), quick_panel_width, quick_panel_height, 15);
    fill_round_rect_alpha(quick_panel_x(), quick_panel_y(), quick_panel_width, quick_panel_height, 15,
                          current_theme.surface, desktop_opacity);
    fill_round_rect(quick_panel_x() + 1, quick_panel_y() + 1, quick_panel_width - 2, 42, 14, current_theme.surface_high);
    draw_text(quick_panel_x() + 18, quick_panel_y() + 18, "QUICK SETTINGS", current_theme.text, 1);
    draw_text_right(quick_panel_x() + quick_panel_width - 18, quick_panel_y() + 18, "F10", current_theme.text_muted, 1);
    for (uint8_t i = 0; i < quick_settings_count; ++i) draw_quick_tile(i);
    draw_text(quick_panel_x() + 18, quick_panel_y() + quick_panel_height - 20, "ENTER CHANGE   ESC CLOSE", current_theme.text_muted, 1);
}
