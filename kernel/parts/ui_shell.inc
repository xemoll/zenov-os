constexpr int32_t taskbar_margin = 14;
constexpr int32_t taskbar_main_width_comfortable = 204;
constexpr int32_t taskbar_main_width_compact = 172;
constexpr int32_t taskbar_tray_width = 150;
constexpr int32_t taskbar_status_width = 72;
constexpr int32_t quick_panel_width = 292;
constexpr int32_t quick_panel_height = 282;
constexpr int32_t quick_tile_width = 126;
constexpr int32_t quick_tile_height = 50;
constexpr int32_t status_panel_width = 300;
constexpr int32_t status_panel_height = 322;

int32_t taskbar_height() { return runtime_compact_density ? 38 : 46; }
int32_t taskbar_y() { return static_cast<int32_t>(height) - taskbar_margin - taskbar_height(); }
int32_t taskbar_main_width() { return runtime_compact_density ? taskbar_main_width_compact : taskbar_main_width_comfortable; }
int32_t taskbar_main_x() {
    return runtime_taskbar_left ? taskbar_margin : (static_cast<int32_t>(width) - taskbar_main_width()) / 2;
}
int32_t taskbar_tray_x() { return static_cast<int32_t>(width) - taskbar_margin - taskbar_tray_width; }
int32_t taskbar_clock_x() { return taskbar_tray_x() + taskbar_status_width; }
int32_t taskbar_clock_width() { return taskbar_tray_width - taskbar_status_width; }
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

bool rtc_read_date(uint8_t& weekday, uint8_t& day, uint8_t& month, uint8_t& year) {
    for (uint32_t retry = 0; retry < 8U; ++retry) {
        if (cmos_read(0x0A) & 0x80U) continue;
        const uint8_t first_weekday = cmos_read(0x06);
        const uint8_t first_day = cmos_read(0x07);
        const uint8_t first_month = cmos_read(0x08);
        const uint8_t first_year = cmos_read(0x09);
        const uint8_t status_b = cmos_read(0x0B);
        if (cmos_read(0x0A) & 0x80U) continue;
        if (first_day != cmos_read(0x07) || first_month != cmos_read(0x08) || first_year != cmos_read(0x09)) continue;
        weekday = first_weekday; day = first_day; month = first_month; year = first_year;
        if ((status_b & 0x04U) == 0U) {
            weekday = bcd_to_binary(weekday); day = bcd_to_binary(day);
            month = bcd_to_binary(month); year = bcd_to_binary(year);
        }
        return weekday >= 1U && weekday <= 7U && day >= 1U && day <= 31U && month >= 1U && month <= 12U;
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

void shell_format_uint(uint32_t value, char* output, uint32_t capacity, const char* suffix = nullptr) {
    if (!output || capacity < 2U) return;
    char reverse[11]; uint32_t count = 0, length = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    while (count && length + 1U < capacity) output[length++] = reverse[--count];
    while (suffix && *suffix && length + 1U < capacity) output[length++] = *suffix++;
    output[length] = 0;
}

void draw_taskbar_button(uint8_t index, ShellIcon icon, bool selected, HoverTarget target) {
    const int32_t size = taskbar_button_size();
    const int32_t x = taskbar_button_x(index);
    const int32_t y = taskbar_y() + (taskbar_height() - size) / 2;
    uint32_t fill = current_theme.surface_low;
    if (selected) fill = current_theme.accent_soft;
    else if (is_hovered(target)) fill = current_theme.surface_high;
    fill_round_rect(x, y, size, size, runtime_compact_density ? 8 : 10, fill);
    const int32_t icon_x = x + (size - 23) / 2;
    const int32_t icon_y = y + (size - 23) / 2;
    draw_shell_icon(icon, icon_x, icon_y, selected ? current_theme.accent : current_theme.text_muted);
    if (selected) fill_round_rect(x + size / 2 - 8, taskbar_y() + taskbar_height() - 4, 16, 2, 1, current_theme.accent);
}

void draw_taskbar() {
    const int32_t radius = runtime_compact_density ? 10 : 13;
    fill_round_rect_alpha(taskbar_main_x(), taskbar_y(), taskbar_main_width(), taskbar_height(), radius,
                          current_theme.surface_low, desktop_opacity);
    draw_round_border(taskbar_main_x(), taskbar_y(), taskbar_main_width(), taskbar_height(), radius, current_theme.border);
    draw_taskbar_button(0, ShellIcon::start, active_view == View::launcher, HoverTarget::taskbar_start);
    draw_taskbar_button(1, ShellIcon::terminal, active_view == View::terminal, HoverTarget::taskbar_terminal);
    draw_taskbar_button(2, ShellIcon::files, active_view == View::files, HoverTarget::taskbar_files);
    draw_taskbar_button(3, ShellIcon::settings, active_view == View::settings, HoverTarget::taskbar_settings);

    fill_round_rect_alpha(taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height(), radius,
                          current_theme.surface_low, desktop_opacity);
    draw_round_border(taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height(), radius, current_theme.border);
    if (quick_settings_open || is_hovered(HoverTarget::taskbar_status)) {
        fill_round_rect(taskbar_tray_x() + 3, taskbar_y() + 3, taskbar_status_width - 4, taskbar_height() - 6,
                        radius > 3 ? radius - 3 : radius, quick_settings_open ? current_theme.accent_soft : current_theme.surface_high);
    }
    if (status_center_open || is_hovered(HoverTarget::taskbar_clock)) {
        fill_round_rect(taskbar_clock_x(), taskbar_y() + 3, taskbar_clock_width() - 3, taskbar_height() - 6,
                        radius > 3 ? radius - 3 : radius, status_center_open ? current_theme.accent_soft : current_theme.surface_high);
    }
    fill_circle_alpha(taskbar_tray_x() + 15, taskbar_y() + taskbar_height() / 2, 3, current_theme.success, 255);
    draw_text(taskbar_tray_x() + 26, taskbar_y() + (runtime_compact_density ? 15 : 19), "Local", current_theme.text_muted, 1);
    fill_rect(taskbar_clock_x() - 1, taskbar_y() + 9, 1, taskbar_height() - 18, current_theme.border);
    uint8_t hour = 0, minute = 0;
    if (rtc_read_time(hour, minute)) {
        const int32_t clock_x = taskbar_clock_x() + 15;
        const int32_t clock_y = taskbar_y() + (runtime_compact_density ? 15 : 19);
        draw_two_digits(clock_x, clock_y, hour, current_theme.text);
        draw_char(clock_x + 13, clock_y, ':', current_theme.text_muted, 1);
        draw_two_digits(clock_x + 19, clock_y, minute, current_theme.text);
    } else draw_text(taskbar_clock_x() + 22, taskbar_y() + 19, "RTC", current_theme.warning, 1);
}

int32_t quick_panel_x() { return static_cast<int32_t>(width) - taskbar_margin - quick_panel_width; }
int32_t quick_panel_y() { return taskbar_y() - quick_panel_height - 10; }
int32_t quick_tile_x(uint8_t index) { return quick_panel_x() + 16 + static_cast<int32_t>(index % 2U) * 134; }
int32_t quick_tile_y(uint8_t index) { return quick_panel_y() + 58 + static_cast<int32_t>(index / 2U) * 58; }

HoverTarget quick_target(uint8_t index) {
    static constexpr HoverTarget targets[quick_settings_count] = {
        HoverTarget::quick_theme, HoverTarget::quick_display, HoverTarget::quick_motion,
        HoverTarget::quick_contrast, HoverTarget::quick_density, HoverTarget::quick_taskbar
    };
    return index < quick_settings_count ? targets[index] : HoverTarget::none;
}

const char* quick_label(uint8_t index) {
    static constexpr const char* labels[quick_settings_count] = {"Theme", "Display", "Motion", "Contrast", "Density", "Taskbar"};
    return index < quick_settings_count ? labels[index] : "";
}

const char* wallpaper_name() { return runtime_wallpaper_style == 1U ? "Calm" : (runtime_wallpaper_style == 2U ? "Plain" : "Flow"); }
const char* theme_name() { return runtime_theme == 1U ? "Graphite" : (runtime_theme == 2U ? "Amber" : "Midnight"); }

void draw_quick_tile(uint8_t index) {
    const int32_t x = quick_tile_x(index), y = quick_tile_y(index);
    const bool focused = quick_settings_focus == index;
    const bool hovered = is_hovered(quick_target(index));
    uint32_t fill = current_theme.surface_low;
    if (focused) fill = current_theme.accent_soft;
    else if (hovered) fill = current_theme.surface_high;
    fill_round_rect(x, y, quick_tile_width, quick_tile_height, 9, fill);
    fill_round_rect(x + 10, y + 10, 30, 30, 8, current_theme.surface_high);
    fill_circle_alpha(x + 25, y + 25, 5, focused ? current_theme.accent : current_theme.text_muted, 255);
    draw_text(x + 50, y + 12, quick_label(index), current_theme.text, 1);
    if (index == 0U) draw_text(x + 50, y + 31, theme_name(), current_theme.text_muted, 1);
    else if (index == 1U) { char mode[16]; format_display_mode(mode); draw_text(x + 50, y + 31, mode, current_theme.text_muted, 1); }
    else if (index == 2U) draw_text(x + 50, y + 31, desktop_animations ? "Smooth" : "Reduced", current_theme.text_muted, 1);
    else if (index == 3U) draw_text(x + 50, y + 31, runtime_high_contrast ? "High" : "Normal", current_theme.text_muted, 1);
    else if (index == 4U) draw_text(x + 50, y + 31, runtime_compact_density ? "Compact" : "Comfort", current_theme.text_muted, 1);
    else draw_text(x + 50, y + 31, runtime_taskbar_left ? "Left" : "Center", current_theme.text_muted, 1);
    if (focused) fill_rect(x + 2, y + 11, 2, quick_tile_height - 22, current_theme.accent);
}

void draw_quick_settings() {
    if (!quick_settings_open) return;
    draw_shadow(quick_panel_x(), quick_panel_y(), quick_panel_width, quick_panel_height, 15);
    fill_round_rect_alpha(quick_panel_x(), quick_panel_y(), quick_panel_width, quick_panel_height, 15,
                          current_theme.surface, desktop_opacity);
    draw_round_border(quick_panel_x(), quick_panel_y(), quick_panel_width, quick_panel_height, 15, current_theme.border);
    fill_round_rect(quick_panel_x() + 1, quick_panel_y() + 1, quick_panel_width - 2, 46, 14, current_theme.surface_low);
    draw_text(quick_panel_x() + 18, quick_panel_y() + 14, "Control Center", current_theme.text, 1);
    draw_text(quick_panel_x() + 18, quick_panel_y() + 31, "System controls", current_theme.text_muted, 1);
    draw_text_right(quick_panel_x() + quick_panel_width - 18, quick_panel_y() + 14, "F10", current_theme.text_muted, 1);
    for (uint8_t i = 0; i < quick_settings_count; ++i) draw_quick_tile(i);
    fill_rect(quick_panel_x() + 16, quick_panel_y() + quick_panel_height - 34, quick_panel_width - 32, 1, current_theme.border);
    draw_text(quick_panel_x() + 18, quick_panel_y() + quick_panel_height - 20, "Enter change   Esc close", current_theme.text_muted, 1);
}

int32_t status_panel_x() { return static_cast<int32_t>(width) - taskbar_margin - status_panel_width; }
int32_t status_panel_y() { return taskbar_y() - status_panel_height - 10; }

void draw_status_value_row(int32_t y, const char* label, const char* value, uint32_t value_color = 0U) {
    const int32_t x = status_panel_x() + 16;
    fill_round_rect(x, y, status_panel_width - 32, 32, 8, current_theme.surface_high);
    draw_text(x + 12, y + 12, label, current_theme.text_muted, 1);
    draw_text_right(x + status_panel_width - 56, y + 12, value, value_color ? value_color : current_theme.text, 1);
}

void draw_status_center() {
    if (!status_center_open) return;
    const int32_t x = status_panel_x(), y = status_panel_y();
    draw_shadow(x, y, status_panel_width, status_panel_height, 15);
    fill_round_rect_alpha(x, y, status_panel_width, status_panel_height, 15, current_theme.surface, desktop_opacity);
    fill_round_rect(x + 1, y + 1, status_panel_width - 2, 44, 14, current_theme.surface_high);
    draw_text(x + 18, y + 18, "System Center", current_theme.text, 1);
    draw_text_right(x + status_panel_width - 18, y + 18, "F11", current_theme.text_muted, 1);

    fill_round_rect(x + 16, y + 56, status_panel_width - 32, 62, 10, current_theme.surface_low);
    uint8_t hour = 0, minute = 0, weekday = 0, day = 0, month = 0, year = 0;
    const bool time_ok = rtc_read_time(hour, minute);
    const bool date_ok = rtc_read_date(weekday, day, month, year);
    static constexpr const char* weekdays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    draw_text(x + 30, y + 72, date_ok ? weekdays[weekday - 1U] : "Local date", current_theme.text_muted, 1);
    if (date_ok) {
        draw_two_digits(x + 30, y + 94, day, current_theme.text);
        draw_char(x + 43, y + 94, '.', current_theme.text_muted, 1);
        draw_two_digits(x + 49, y + 94, month, current_theme.text);
        draw_text(x + 63, y + 94, ".20", current_theme.text_muted, 1);
        draw_two_digits(x + 81, y + 94, year, current_theme.text);
    }
    if (time_ok) {
        draw_two_digits(x + status_panel_width - 82, y + 84, hour, current_theme.accent);
        draw_char(x + status_panel_width - 69, y + 84, ':', current_theme.text_muted, 1);
        draw_two_digits(x + status_panel_width - 63, y + 84, minute, current_theme.accent);
    }

    char uptime[20], memory[20], storage_generation[20], packages[24];
    shell_format_uint(timer_ticks / 100U, uptime, sizeof(uptime), "S");
    shell_format_uint(heap::free_bytes() / 1024U, memory, sizeof(memory), "K free");
    shell_format_uint(storage::browser_generation(), storage_generation, sizeof(storage_generation));
    uint32_t length = 0;
    char installed[12], targets[12];
    shell_format_uint(package_manager::database.count, installed, sizeof(installed));
    shell_format_uint(package_repository::target_count(), targets, sizeof(targets));
    while (installed[length] && length + 1U < sizeof(packages)) { packages[length] = installed[length]; ++length; }
    if (length + 3U < sizeof(packages)) { packages[length++] = ' '; packages[length++] = '/'; packages[length++] = ' '; }
    uint32_t target_at = 0;
    while (targets[target_at] && length + 1U < sizeof(packages)) packages[length++] = targets[target_at++];
    packages[length] = 0;

    draw_status_value_row(y + 128, "Uptime", uptime);
    draw_status_value_row(y + 166, "Memory", memory);
    draw_status_value_row(y + 204, "Storage gen", storage_generation);
    draw_status_value_row(y + 242, "Packages", packages);
    const bool trust_ready = security_guard::enforcement_ready && zgdb::ready && security_audit::ready;
    fill_circle_alpha(x + 26, y + 296, 4, trust_ready ? current_theme.success : current_theme.danger, 255);
    draw_text(x + 38, y + 293, trust_ready ? "Trust chain ready" : "Security locked", trust_ready ? current_theme.text : current_theme.danger, 1);
}
