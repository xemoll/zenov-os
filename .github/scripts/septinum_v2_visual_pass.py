from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)


def replace_function(text: str, signature: str, replacement: str, label: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"{label}: signature not found: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"{label}: opening brace not found")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[:start] + replacement.rstrip() + text[index + 1:]
    raise SystemExit(f"{label}: closing brace not found")


# Geometry
path = "kernel/parts/ui_graphics_base.inc"
text = read(path)
text = replace_once(text,
    "constexpr int32_t topbar_x = 24, topbar_y = 16, topbar_width = 848;",
    "constexpr int32_t topbar_x = 18, topbar_y = 8, topbar_width = 860;",
    "topbar geometry")
write(path, text)

# Wallpaper, top shell, chrome
path = "kernel/parts/ui_theme.inc"
text = read(path)
text = replace_function(text, "void draw_wallpaper()", r'''void draw_wallpaper() {
    vertical_gradient(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height),
                      current_theme.canvas_top, current_theme.canvas_bottom);
    fill_rect_alpha(0, 0, static_cast<int32_t>(width), 1, current_theme.accent, 34);
    if (runtime_wallpaper_style == 2U) return;
    const uint32_t quiet_line = mix(current_theme.surface_high, current_theme.accent, 1U, 7U);
    if (runtime_wallpaper_style == 1U) {
        fill_circle_alpha(730, 96, 174, current_theme.glow_secondary, 8);
        fill_circle_alpha(84, 446, 202, current_theme.glow_primary, 6);
        fill_rect_alpha(42, 116, 812, 1, quiet_line, 18);
        fill_rect_alpha(42, 390, 812, 1, quiet_line, 12);
        return;
    }
    fill_circle_alpha(86, 78, 218, current_theme.glow_primary, 11);
    fill_circle_alpha(770, 436, 266, current_theme.glow_secondary, 9);
    fill_circle_alpha(690, 82, 126, current_theme.accent, 5);
    fill_circle_alpha(228, 440, 118, current_theme.glow_secondary, 4);
    fill_rect_alpha(42, 116, 812, 1, quiet_line, 22);
    fill_rect_alpha(42, 390, 812, 1, quiet_line, 15);
    fill_rect_alpha(294, 52, 1, 356, quiet_line, 10);
    fill_rect_alpha(602, 52, 1, 356, quiet_line, 8);
}''', "wallpaper")

helper = r'''const char* active_view_label() {
    if (active_view == View::terminal) return "Terminal";
    if (active_view == View::files) return "Files";
    if (active_view == View::settings) return "Settings";
    return "Start";
}

uint8_t active_view_slot() {
    if (active_view == View::terminal) return 0U;
    if (active_view == View::files) return 1U;
    if (active_view == View::settings) return 2U;
    return 3U;
}

void draw_view_slots(int32_t x, int32_t y) {
    const uint8_t active_slot = active_view_slot();
    for (uint8_t slot = 0; slot < 4U; ++slot) {
        const int32_t slot_x = x + static_cast<int32_t>(slot) * 18;
        if (slot == active_slot) fill_round_rect(slot_x, y, 14, 5, 2, current_theme.accent);
        else fill_circle_alpha(slot_x + 4, y + 2, 2, current_theme.text_muted, 150);
    }
}

'''
text = replace_once(text, "void draw_topbar() {", helper + "void draw_topbar() {", "topbar helper")
text = replace_function(text, "void draw_topbar()", r'''void draw_topbar() {
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 2U);
    fill_round_rect_alpha(topbar_x, topbar_y, 146, 22, 8, current_theme.surface_low, 224);
    draw_round_border(topbar_x, topbar_y, 146, 22, 8, shell_border);
    fill_round_rect(topbar_x + 7, topbar_y + 5, 12, 12, 4, current_theme.accent_soft);
    fill_circle_alpha(topbar_x + 13, topbar_y + 11, 2, current_theme.accent, 255);
    draw_text(topbar_x + 27, topbar_y + 9, "ZenovOS", current_theme.text, 1);
    fill_circle_alpha(topbar_x + 126, topbar_y + 11, 2, current_theme.success, 255);

    const int32_t center_x = (static_cast<int32_t>(width) - 292) / 2;
    fill_round_rect_alpha(center_x, topbar_y, 292, 22, 8, current_theme.surface_low, 224);
    draw_round_border(center_x, topbar_y, 292, 22, 8, shell_border);
    draw_text(center_x + 16, topbar_y + 9, active_view_label(), current_theme.text, 1);
    draw_text(center_x + 92, topbar_y + 9, "Workspace", current_theme.text_muted, 1);
    draw_view_slots(center_x + 202, topbar_y + 9);

    const int32_t right_x = topbar_x + topbar_width - 206;
    fill_round_rect_alpha(right_x, topbar_y, 206, 22, 8, current_theme.surface_low, 224);
    draw_round_border(right_x, topbar_y, 206, 22, 8, shell_border);
    fill_circle_alpha(right_x + 13, topbar_y + 11, 2, current_theme.success, 255);
    draw_text(right_x + 23, topbar_y + 9, "Trusted session", current_theme.text_muted, 1);
    draw_text_right(right_x + 190, topbar_y + 9, zenov_generated::kVersion, current_theme.text, 1);
}''', "topbar")
text = replace_function(text, "void draw_window_chrome(const char* title, const char* subtitle)", r'''void draw_window_chrome(const char* title, const char* subtitle) {
    const int32_t radius = static_cast<int32_t>(desktop_radius);
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 3U);
    draw_shadow(window_x, window_y, window_width, window_height, radius);
    fill_round_rect_alpha(window_x, window_y, window_width, window_height, radius,
                          current_theme.surface, desktop_opacity);
    draw_round_border(window_x, window_y, window_width, window_height, radius, shell_border);
    fill_round_rect(window_x + 1, window_y + 1, window_width - 2, titlebar_height + 5,
                    radius > 1 ? radius - 1 : 0, current_theme.surface_low);
    fill_rect_alpha(window_x + 18, window_y + 1, 96, 2, current_theme.accent, 190);
    fill_rect(window_x + 1, window_y + titlebar_height + 2, window_width - 2, 1, shell_border);
    fill_round_rect(window_x + 14, window_y + 10, 26, 26, 8, current_theme.accent_soft);
    draw_shell_icon(active_view == View::terminal ? ShellIcon::terminal :
                    (active_view == View::files ? ShellIcon::files : ShellIcon::settings),
                    window_x + 16, window_y + 12, current_theme.accent);
    draw_text_ellipsis(window_x + 52, window_y + 16, 210, title, current_theme.text, 1);
    const int32_t subtitle_x = window_x + window_width - 202;
    fill_round_rect(subtitle_x, window_y + 10, 112, 24, 7, current_theme.surface_high);
    draw_text_center(subtitle_x + 56, window_y + 18, subtitle, current_theme.text_muted, 1);
    fill_circle_alpha(window_x + window_width - 68, window_y + 22, 4, current_theme.surface_high, 255);
    fill_circle_alpha(window_x + window_width - 46, window_y + 22, 4, current_theme.surface_high, 255);
    fill_circle_alpha(window_x + window_width - 24, window_y + 22, 4, current_theme.accent_soft, 255);
}''', "window chrome")
write(path, text)

# Taskbar and Control Center
path = "kernel/parts/ui_shell.inc"
text = read(path)
for old, new in {
    "constexpr int32_t taskbar_main_width_comfortable = 204;": "constexpr int32_t taskbar_main_width_comfortable = 220;",
    "constexpr int32_t taskbar_main_width_compact = 172;": "constexpr int32_t taskbar_main_width_compact = 188;",
    "constexpr int32_t taskbar_tray_width = 150;": "constexpr int32_t taskbar_tray_width = 168;",
    "constexpr int32_t taskbar_status_width = 72;": "constexpr int32_t taskbar_status_width = 84;",
    "constexpr int32_t quick_panel_width = 292;": "constexpr int32_t quick_panel_width = 320;",
    "constexpr int32_t quick_panel_height = 282;": "constexpr int32_t quick_panel_height = 306;",
    "constexpr int32_t quick_tile_width = 126;": "constexpr int32_t quick_tile_width = 136;",
    "constexpr int32_t quick_tile_height = 50;": "constexpr int32_t quick_tile_height = 52;",
    "int32_t quick_tile_x(uint8_t index) { return quick_panel_x() + 16 + static_cast<int32_t>(index % 2U) * 134; }": "int32_t quick_tile_x(uint8_t index) { return quick_panel_x() + 16 + static_cast<int32_t>(index % 2U) * 144; }",
    "int32_t quick_tile_y(uint8_t index) { return quick_panel_y() + 58 + static_cast<int32_t>(index / 2U) * 58; }": "int32_t quick_tile_y(uint8_t index) { return quick_panel_y() + 94 + static_cast<int32_t>(index / 2U) * 58; }",
}.items():
    text = replace_once(text, old, new, old)

text = replace_function(text, "void draw_taskbar_button(uint8_t index, ShellIcon icon, bool selected, HoverTarget target)", r'''void draw_taskbar_button(uint8_t index, ShellIcon icon, bool selected, HoverTarget target) {
    const int32_t size = taskbar_button_size();
    const int32_t x = taskbar_button_x(index);
    const int32_t y = taskbar_y() + (taskbar_height() - size) / 2;
    if (selected) fill_round_rect(x, y, size, size, runtime_compact_density ? 8 : 10, current_theme.accent_soft);
    else if (is_hovered(target)) fill_round_rect(x, y, size, size, runtime_compact_density ? 8 : 10, current_theme.surface_high);
    draw_shell_icon(icon, x + (size - 23) / 2, y + (size - 23) / 2,
                    selected ? current_theme.accent : current_theme.text_muted);
    if (selected) {
        fill_round_rect(x + size / 2 - 8, taskbar_y() + taskbar_height() - 4, 16, 2, 1, current_theme.accent);
        fill_circle_alpha(x + size / 2, y + 4, 8, current_theme.accent, 14);
    }
}''', "taskbar button")
text = replace_function(text, "void draw_taskbar()", r'''void draw_taskbar() {
    const int32_t radius = runtime_compact_density ? 10 : 13;
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 3U);
    draw_shadow(taskbar_main_x(), taskbar_y(), taskbar_main_width(), taskbar_height(), radius);
    fill_round_rect_alpha(taskbar_main_x(), taskbar_y(), taskbar_main_width(), taskbar_height(), radius,
                          current_theme.surface_low, desktop_opacity);
    draw_round_border(taskbar_main_x(), taskbar_y(), taskbar_main_width(), taskbar_height(), radius, shell_border);
    draw_taskbar_button(0, ShellIcon::start, active_view == View::launcher, HoverTarget::taskbar_start);
    draw_taskbar_button(1, ShellIcon::terminal, active_view == View::terminal, HoverTarget::taskbar_terminal);
    draw_taskbar_button(2, ShellIcon::files, active_view == View::files, HoverTarget::taskbar_files);
    draw_taskbar_button(3, ShellIcon::settings, active_view == View::settings, HoverTarget::taskbar_settings);
    draw_shadow(taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height(), radius);
    fill_round_rect_alpha(taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height(), radius,
                          current_theme.surface_low, desktop_opacity);
    draw_round_border(taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height(), radius, shell_border);
    if (quick_settings_open || is_hovered(HoverTarget::taskbar_status))
        fill_round_rect(taskbar_tray_x() + 3, taskbar_y() + 3, taskbar_status_width - 4, taskbar_height() - 6,
                        radius > 3 ? radius - 3 : radius, quick_settings_open ? current_theme.accent_soft : current_theme.surface_high);
    if (status_center_open || is_hovered(HoverTarget::taskbar_clock))
        fill_round_rect(taskbar_clock_x(), taskbar_y() + 3, taskbar_clock_width() - 3, taskbar_height() - 6,
                        radius > 3 ? radius - 3 : radius, status_center_open ? current_theme.accent_soft : current_theme.surface_high);
    fill_circle_alpha(taskbar_tray_x() + 15, taskbar_y() + taskbar_height() / 2, 3, current_theme.success, 255);
    draw_text(taskbar_tray_x() + 26, taskbar_y() + (runtime_compact_density ? 15 : 19), "Secure",
              quick_settings_open ? current_theme.text : current_theme.text_muted, 1);
    fill_rect(taskbar_clock_x() - 1, taskbar_y() + 9, 1, taskbar_height() - 18, shell_border);
    uint8_t hour = 0, minute = 0;
    if (rtc_read_time(hour, minute)) {
        const int32_t clock_x = taskbar_clock_x() + 15;
        const int32_t clock_y = taskbar_y() + (runtime_compact_density ? 15 : 19);
        draw_two_digits(clock_x, clock_y, hour, current_theme.text);
        draw_char(clock_x + 13, clock_y, ':', status_center_open ? current_theme.text : current_theme.text_muted, 1);
        draw_two_digits(clock_x + 19, clock_y, minute, current_theme.text);
    } else draw_text(taskbar_clock_x() + 22, taskbar_y() + 19, "RTC", current_theme.warning, 1);
}''', "taskbar")

quick_helper = r'''ShellIcon quick_icon(uint8_t index) {
    if (index == 1U) return ShellIcon::display;
    if (index == 4U) return ShellIcon::apps;
    if (index == 5U) return ShellIcon::start;
    return ShellIcon::settings;
}

uint32_t quick_color(uint8_t index) {
    if (index == 1U) return rgb(74,156,231);
    if (index == 2U) return rgb(151,122,255);
    if (index == 3U) return current_theme.warning;
    if (index == 4U) return current_theme.success;
    return current_theme.accent;
}

'''
text = replace_once(text, "void draw_quick_tile(uint8_t index) {", quick_helper + "void draw_quick_tile(uint8_t index) {", "quick helper")
text = replace_function(text, "void draw_quick_tile(uint8_t index)", r'''void draw_quick_tile(uint8_t index) {
    const int32_t x = quick_tile_x(index), y = quick_tile_y(index);
    const bool focused = quick_settings_focus == index;
    const bool hovered = is_hovered(quick_target(index));
    const uint32_t color = quick_color(index);
    uint32_t fill = current_theme.surface_low;
    if (focused) fill = mix(current_theme.surface_high, color, 1U, 5U);
    else if (hovered) fill = current_theme.surface_high;
    fill_round_rect(x, y, quick_tile_width, quick_tile_height, 10, fill);
    fill_round_rect(x + 9, y + 10, 32, 32, 9, mix(current_theme.surface_high, color, 2U, 5U));
    draw_shell_icon(quick_icon(index), x + 14, y + 15, color);
    draw_text(x + 50, y + 13, quick_label(index), current_theme.text, 1);
    const uint32_t secondary = focus_text_color(focused);
    if (index == 0U) draw_text(x + 50, y + 32, theme_name(), secondary, 1);
    else if (index == 1U) { char mode[16]; format_display_mode(mode); draw_text(x + 50, y + 32, mode, secondary, 1); }
    else if (index == 2U) draw_text(x + 50, y + 32, desktop_animations ? "Smooth" : "Reduced", secondary, 1);
    else if (index == 3U) draw_text(x + 50, y + 32, runtime_high_contrast ? "High" : "Normal", secondary, 1);
    else if (index == 4U) draw_text(x + 50, y + 32, runtime_compact_density ? "Compact" : "Comfort", secondary, 1);
    else draw_text(x + 50, y + 32, runtime_taskbar_left ? "Left" : "Center", secondary, 1);
    fill_round_rect(x + quick_tile_width - 24, y + 12, 14, 6, 3, focused ? color : current_theme.surface_high);
    if (focused) { fill_rect(x + 2, y + 12, 2, quick_tile_height - 24, color); draw_focus_outline(x, y, quick_tile_width, quick_tile_height, current_theme.accent); }
}''', "quick tile")
text = replace_function(text, "void draw_quick_settings()", r'''void draw_quick_settings() {
    if (!quick_settings_open) return;
    const int32_t x = quick_panel_x(), y = quick_panel_y();
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 3U);
    draw_shadow(x, y, quick_panel_width, quick_panel_height, 16);
    fill_round_rect_alpha(x, y, quick_panel_width, quick_panel_height, 16, current_theme.surface, desktop_opacity);
    draw_round_border(x, y, quick_panel_width, quick_panel_height, 16, shell_border);
    fill_round_rect(x + 1, y + 1, quick_panel_width - 2, 46, 15, current_theme.surface_low);
    draw_text(x + 18, y + 14, "Control Center", current_theme.text, 1);
    draw_text(x + 18, y + 31, "System controls", current_theme.text_muted, 1);
    draw_text_right(x + quick_panel_width - 18, y + 14, "F10", current_theme.text_muted, 1);
    fill_round_rect(x + 16, y + 52, quick_panel_width - 32, 32, 9, current_theme.surface_high);
    const bool trust_ready = security_guard::enforcement_ready && zgdb::ready && security_audit::ready;
    fill_circle_alpha(x + 30, y + 68, 4, trust_ready ? current_theme.success : current_theme.danger, 255);
    draw_text(x + 42, y + 65, trust_ready ? "Protection active" : "Protection locked", current_theme.text, 1);
    draw_text_right(x + quick_panel_width - 28, y + 65, "32 modes", current_theme.text_muted, 1);
    for (uint8_t i = 0; i < quick_settings_count; ++i) draw_quick_tile(i);
    fill_rect(x + 16, y + quick_panel_height - 34, quick_panel_width - 32, 1, shell_border);
    draw_text(x + 18, y + quick_panel_height - 20, "Enter change", current_theme.text_muted, 1);
    draw_text_right(x + quick_panel_width - 18, y + quick_panel_height - 20, "Esc close", current_theme.text_muted, 1);
}''', "quick settings")
write(path, text)

# Terminal, launcher morph, copy cleanup
path = "kernel/parts/ui_apps.inc"
text = read(path)
for old, new in {
    "constexpr int32_t launcher_panel_x = 208;": "constexpr int32_t launcher_panel_x = 184;",
    "constexpr int32_t launcher_panel_width = 480;": "constexpr int32_t launcher_panel_width = 528;",
    "constexpr int32_t launcher_search_x = launcher_panel_x + 22;": "constexpr int32_t launcher_search_x = launcher_panel_x + 20;",
    "constexpr int32_t launcher_search_y = launcher_panel_y + 54;": "constexpr int32_t launcher_search_y = launcher_panel_y + 58;",
    "constexpr int32_t launcher_search_width = launcher_panel_width - 44;": "constexpr int32_t launcher_search_width = launcher_panel_width - 40;",
    "constexpr int32_t launcher_search_height = 36;": "constexpr int32_t launcher_search_height = 40;",
    "constexpr int32_t launcher_card_width = 436;": "constexpr int32_t launcher_card_width = 488;",
    "constexpr int32_t launcher_card_height = 38;": "constexpr int32_t launcher_card_height = 40;",
    "int32_t launcher_card_x(uint8_t) { return launcher_panel_x + 22; }": "int32_t launcher_card_x(uint8_t) { return launcher_panel_x + 20; }",
    "int32_t launcher_card_y(uint8_t index) { return launcher_panel_y + 106 + static_cast<int32_t>(launcher_visible_rank(index)) * 40; }": "int32_t launcher_card_y(uint8_t index) { return launcher_panel_y + 128 + static_cast<int32_t>(launcher_visible_rank(index)) * 42; }",
    "draw_text_right(x + settings_content_width - 16, window_y + 196, \"800X600\", current_theme.text, 1);": "draw_text_right(x + settings_content_width - 16, window_y + 196, \"896X504\", current_theme.text, 1);",
}.items():
    text = replace_once(text, old, new, old)

text = replace_function(text, "void render_terminal_home()", r'''void render_terminal_home() {
    const int32_t panel_x = window_x + terminal_panel_x_offset;
    const int32_t panel_y = window_y + terminal_panel_y_offset;
    fill_round_rect(panel_x, panel_y, terminal_panel_width, terminal_panel_height, 10, current_theme.terminal);
    fill_round_rect(panel_x, panel_y + 18, 3, terminal_panel_height - 36, 1, current_theme.accent);
    fill_circle_alpha(panel_x + terminal_panel_width - 54, panel_y + 42, 48, current_theme.accent, 7);
    draw_text(panel_x + 20, panel_y + 24, "ZenovOS", current_theme.text, 2);
    fill_circle_alpha(panel_x + 116, panel_y + 32, 3, current_theme.success, 255);
    draw_text(panel_x + 128, panel_y + 29, "Ready for commands", current_theme.text_muted, 1);
    fill_round_rect(panel_x + terminal_panel_width - 82, panel_y + 18, 62, 24, 7, current_theme.surface_high);
    draw_text_center(panel_x + terminal_panel_width - 51, panel_y + 26, "LOCAL", current_theme.text_muted, 1);
    fill_rect(panel_x + 20, panel_y + 62, terminal_panel_width - 40, 1, current_theme.border);
    draw_text(panel_x + 20, panel_y + 80, "F6 Files   F7 Settings   F8 Start   F10 Control Center", current_theme.text_muted, 1);
    fill_round_rect(panel_x + 16, panel_y + 112, terminal_panel_width - 32, 94, 10, current_theme.surface_low);
    draw_text(panel_x + 30, panel_y + 132, "Current session", current_theme.text_muted, 1);
    fill_round_rect(panel_x + 28, panel_y + 156, terminal_panel_width - 56, 32, 8, current_theme.terminal);
    draw_console_content_row(console::row, 0, panel_x + 38, panel_y + 168);
    draw_terminal_caret(panel_x + 38, panel_y + 168, console::row);
    fill_round_rect(panel_x + 20, panel_y + 232, 176, 34, 9, current_theme.surface_low);
    fill_round_rect(panel_x + 206, panel_y + 232, 270, 34, 9, current_theme.surface_low);
    draw_text(panel_x + 32, panel_y + 245, "Type help for commands", current_theme.text, 1);
    draw_text(panel_x + 218, panel_y + 245, "Tab complete   Arrows history   Esc clear", current_theme.text_muted, 1);
    draw_text(panel_x + 20, panel_y + 296, "Local shell  /  verified system session", current_theme.text_muted, 1);
}''', "terminal home")

morph_helpers = r'''constexpr int32_t launcher_panel_height_for_count(uint8_t count) {
    return count == 0U ? 174 : 174 + static_cast<int32_t>(count) * 42;
}

int32_t launcher_panel_height_current() { return launcher_panel_height_for_count(launcher_match_count()); }
int32_t launcher_footer_y() { return launcher_panel_y + launcher_panel_height_current() - 32; }

uint32_t launcher_item_color(uint8_t index) {
    if (index == 1U) return rgb(74,156,231);
    if (index == 2U) return rgb(151,122,255);
    if (index == 3U) return rgb(68,191,184);
    if (index == 4U) return current_theme.success;
    if (index == 5U) return current_theme.warning;
    return current_theme.accent;
}

'''
text = replace_once(text, "void draw_launcher_card(uint8_t index) {", morph_helpers + "void draw_launcher_card(uint8_t index) {", "launcher helpers")
text = replace_function(text, "void draw_launcher_card(uint8_t index)", r'''void draw_launcher_card(uint8_t index) {
    if (!launcher_item_matches(index)) return;
    const int32_t x = launcher_card_x(index);
    const int32_t y = launcher_card_y(index) + ui_scene_offset_y;
    const bool focused = launcher_focus == index;
    const bool hovered = is_hovered(launcher_target(index));
    const uint32_t color = launcher_item_color(index);
    uint32_t fill = current_theme.surface_low;
    if (focused) fill = mix(current_theme.surface_high, color, 1U, 5U);
    else if (hovered) fill = current_theme.surface_high;
    fill_round_rect(x, y, launcher_card_width, launcher_card_height, 10, fill);
    fill_round_rect(x + 8, y + 6, 30, 28, 8, mix(current_theme.surface_high, color, 2U, 5U));
    draw_shell_icon(launcher_icons[index], x + 11, y + 8, color);
    draw_text(x + 50, y + 16, launcher_names[index], current_theme.text, 1);
    draw_text_right_ellipsis(x + launcher_card_width - 30, y + 16, 174, launcher_details[index], focus_text_color(focused), 1);
    draw_char(x + launcher_card_width - 17, y + 16, '>', focused ? color : current_theme.text_muted, 1);
    if (focused) { fill_rect(x + 2, y + 9, 2, 22, color); draw_focus_outline(x, y, launcher_card_width, launcher_card_height, current_theme.accent); }
}''', "launcher card")
text = replace_function(text, "void render_launcher()", r'''void render_launcher() {
    const int32_t y = launcher_panel_y + ui_scene_offset_y;
    const int32_t panel_height = launcher_panel_height_current();
    const int32_t footer_y = launcher_footer_y() + ui_scene_offset_y;
    const uint32_t shell_border = mix(current_theme.border, current_theme.surface, 1U, 3U);
    draw_shadow(launcher_panel_x, y, launcher_panel_width, panel_height, 17);
    fill_round_rect_alpha(launcher_panel_x, y, launcher_panel_width, panel_height, 17, current_theme.surface, desktop_opacity);
    draw_round_border(launcher_panel_x, y, launcher_panel_width, panel_height, 17, shell_border);
    fill_circle_alpha(launcher_panel_x + launcher_panel_width - 52, y + 64, 44, current_theme.accent, 7);
    fill_round_rect(launcher_panel_x + 1, y + 1, launcher_panel_width - 2, 48, 16, current_theme.surface_low);
    fill_round_rect(launcher_panel_x + 16, y + 11, 28, 28, 8, current_theme.accent_soft);
    draw_shell_icon(ShellIcon::start, launcher_panel_x + 19, y + 14, current_theme.accent);
    draw_text(launcher_panel_x + 54, y + 15, "Zenov", current_theme.text, 1);
    draw_text(launcher_panel_x + 54, y + 31, "Command palette", current_theme.text_muted, 1);
    fill_round_rect(launcher_panel_x + launcher_panel_width - 78, y + 12, 58, 24, 7, current_theme.surface_high);
    draw_text_center(launcher_panel_x + launcher_panel_width - 49, y + 20, "0.1.1", current_theme.text_muted, 1);
    fill_round_rect(launcher_search_x, launcher_search_y + ui_scene_offset_y, launcher_search_width, launcher_search_height, 11, current_theme.surface_low);
    fill_round_rect(launcher_search_x + 8, launcher_search_y + 8 + ui_scene_offset_y, 24, 24, 7, current_theme.surface_high);
    draw_char(launcher_search_x + 17, launcher_search_y + 17 + ui_scene_offset_y, '>', current_theme.accent, 1);
    if (launcher_query_length) draw_text(launcher_search_x + 42, launcher_search_y + 17 + ui_scene_offset_y, launcher_query, current_theme.text, 1);
    else draw_text(launcher_search_x + 42, launcher_search_y + 17 + ui_scene_offset_y, "Search apps, tools and settings", current_theme.text_muted, 1);
    draw_text(launcher_panel_x + 22, y + 116, launcher_query_length ? "Matching results" : "Applications and tools", current_theme.text_muted, 1);
    draw_uint(launcher_panel_x + launcher_panel_width - 34, y + 116, launcher_match_count(), current_theme.text_muted);
    for (uint8_t i = 0; i < launcher_item_count; ++i) draw_launcher_card(i);
    fill_rect(launcher_panel_x + 20, footer_y - 10, launcher_panel_width - 40, 1, shell_border);
    const bool trust_ready = security_guard::enforcement_ready && zgdb::ready && security_audit::ready;
    fill_circle_alpha(launcher_panel_x + 30, footer_y + 12, 3, trust_ready ? current_theme.success : current_theme.danger, 255);
    draw_text(launcher_panel_x + 40, footer_y + 9, trust_ready ? "Security ready" : "Security locked", current_theme.text, 1);
    draw_text_right(launcher_panel_x + launcher_panel_width - 20, footer_y + 9, "Enter open", current_theme.text_muted, 1);
}''', "launcher")
write(path, text)

# Files wording
path = "kernel/parts/ui_files.inc"
text = read(path)
for old, new in {
    'constexpr const char* files_place_names[5] = {"ROOT", "APPS", "CONFIG", "SECURITY", "SYSTEM"};': 'constexpr const char* files_place_names[5] = {"Root", "Apps", "Config", "Security", "System"};',
    'draw_window_chrome("ZEN FILES", "ZENOVFS1");': 'draw_window_chrome("Files", "ZenovFS");',
    'draw_text(window_x + 16, window_y + 68, "PLACES", current_theme.text_muted, 1);': 'draw_text(window_x + 16, window_y + 68, "Places", current_theme.text_muted, 1);',
    'draw_text(content_x + 8, window_y + 108, "NAME", current_theme.text_muted, 1);': 'draw_text(content_x + 8, window_y + 108, "Name", current_theme.text_muted, 1);',
    'draw_text(content_x + 230, window_y + 108, "TYPE", current_theme.text_muted, 1);': 'draw_text(content_x + 230, window_y + 108, "Type", current_theme.text_muted, 1);',
    'draw_text_right(content_x + files_content_width - 8, window_y + 108, "SIZE", current_theme.text_muted, 1);': 'draw_text_right(content_x + files_content_width - 8, window_y + 108, "Size", current_theme.text_muted, 1);',
    'draw_text(content_x + 8, window_y + 354, "UP/DOWN SELECT  ENTER OPEN  BACKSPACE UP", current_theme.text_muted, 1);': 'draw_text(content_x + 8, window_y + 354, "Arrows select   Enter open   Backspace up", current_theme.text_muted, 1);',
    'draw_text(content_x + 8, window_y + 374, "GEN", current_theme.text_muted, 1);': 'draw_text(content_x + 8, window_y + 374, "Generation", current_theme.text_muted, 1);',
}.items():
    text = replace_once(text, old, new, old)
write(path, text)

# Runtime contracts
path = "kernel/parts/ui_runtime.inc"
text = read(path)
text = replace_once(text,
    "        topbar_x == 24 && topbar_x + topbar_width == static_cast<int32_t>(width) - 24 &&",
    "        topbar_x == 18 && topbar_y == 8 && topbar_x + topbar_width == static_cast<int32_t>(width) - 18 &&",
    "widescreen topbar contract")
contract = r'''constexpr bool septinum_v2_visual_contract_ok() {
    return topbar_x == 18 && topbar_y == 8 && topbar_width == 860 &&
        taskbar_main_width_comfortable == 220 && taskbar_tray_width == 168 &&
        quick_panel_width == 320 && quick_panel_height == 306 &&
        quick_tile_width == 136 && quick_tile_height == 52 &&
        launcher_panel_x == 184 && launcher_panel_width == 528 &&
        launcher_search_height == 40 && launcher_card_width == 488 &&
        launcher_panel_height_for_count(0U) == 174 &&
        launcher_panel_height_for_count(1U) == 216 &&
        launcher_panel_height_for_count(6U) == 426;
}

'''
text = replace_once(text, "constexpr bool display_catalog_2k_contract_ok() {", contract + "constexpr bool display_catalog_2k_contract_ok() {", "visual contract")
text = replace_once(text,
    "    const bool catalog_2k_ok = display_catalog_2k_contract_ok();",
    "    const bool septinum_v2_ok = septinum_v2_visual_contract_ok();\n    const bool catalog_2k_ok = display_catalog_2k_contract_ok();",
    "visual contract evaluation")
text = replace_once(text,
    "    serial::line(cohesive_shell_ok ? \"UI_SEPTINUM_SHELL_OK\" : \"UI_SEPTINUM_SHELL_FAILED\");",
    "    serial::line(cohesive_shell_ok ? \"UI_SEPTINUM_SHELL_OK\" : \"UI_SEPTINUM_SHELL_FAILED\");\n"
    "    serial::line(septinum_v2_ok ? \"UI_SEPTINUM_V2_OK\" : \"UI_SEPTINUM_V2_FAILED\");\n"
    "    serial::line(septinum_v2_ok ? \"UI_LAUNCHER_MORPH_OK\" : \"UI_LAUNCHER_MORPH_FAILED\");",
    "visual markers")
text = replace_once(text,
    "return clipping && runtime_preferences_loaded && bounds_ok && geometry_ok && cohesive_shell_ok && navigation_ok &&\n        display_ok",
    "return clipping && runtime_preferences_loaded && bounds_ok && geometry_ok && cohesive_shell_ok && septinum_v2_ok && navigation_ok &&\n        display_ok",
    "visual init gate")
write(path, text)

# Display evidence updates
path = "tests/qemu_display_ui.sh"
text = read(path)
text = replace_once(text,
    "  wait_for_serial \"UI_SEPTINUM_SHELL_OK\" || { echo quit; return 1; }",
    "  wait_for_serial \"UI_SEPTINUM_SHELL_OK\" || { echo quit; return 1; }\n"
    "  wait_for_serial \"UI_SEPTINUM_V2_OK\" || { echo quit; return 1; }\n"
    "  wait_for_serial \"UI_LAUNCHER_MORPH_OK\" || { echo quit; return 1; }",
    "visual test markers")
text = replace_once(text,
    "start = coverage('desktop-1920x1080.ppm', 'start-1920x1080.ppm', (208, 10, 480, 426))\ncontrol = coverage('desktop-2560x1440.ppm', 'control-center-2560x1440.ppm', (590, 152, 292, 282))",
    "start = coverage('desktop-1920x1080.ppm', 'start-1920x1080.ppm', (184, 10, 528, 426))\ncontrol = coverage('desktop-2560x1440.ppm', 'control-center-2560x1440.ppm', (562, 128, 320, 306))",
    "popup coverage geometry")
text = replace_once(text,
    "print('ui-popup-vertical-coverage: OK start=3/3 control-center=3/3')\nPY\nsha256sum",
    r'''print('ui-popup-vertical-coverage: OK start=3/3 control-center=3/3')

width, height, desktop = ppm('desktop-1024x768.ppm')
_, _, full = ppm('start-1024x768.ppm')
_, _, filtered = ppm('start-search-1024x768.ppm')
viewport_y = 96
x0, x1 = 184 * width // 896, (184 + 528) * width // 896
y0, y1 = viewport_y + 10 * 576 // 504, viewport_y + 438 * 576 // 504

def changed_bottom(frame):
    bottom = -1
    for py in range(y0, min(height, y1)):
        changed = 0
        for px in range(x0, min(width, x1)):
            offset = (py * width + px) * 3
            if desktop[offset:offset+3] != frame[offset:offset+3]:
                changed += 1
        if changed >= 24:
            bottom = py
    return bottom

full_bottom = changed_bottom(full)
filtered_bottom = changed_bottom(filtered)
if full_bottom < 0 or filtered_bottom < 0 or full_bottom - filtered_bottom < 100:
    raise SystemExit(f'launcher morph failed full_bottom={full_bottom} filtered_bottom={filtered_bottom}')
(out / 'launcher-morph-evidence.txt').write_text(
    f'full_bottom={full_bottom}\nfiltered_bottom={filtered_bottom}\ndelta={full_bottom-filtered_bottom}\n',
    encoding='utf-8')
print(f'ui-launcher-morph-evidence: OK delta={full_bottom-filtered_bottom}')
PY
sha256sum''',
    "launcher morph image gate")
text = replace_once(text,
    "UI_START_SYSTEM_TOOLS_OK UI_SEPTINUM_SHELL_OK UI_FONT_ATLAS_OK",
    "UI_START_SYSTEM_TOOLS_OK UI_SEPTINUM_SHELL_OK UI_SEPTINUM_V2_OK UI_LAUNCHER_MORPH_OK UI_FONT_ATLAS_OK",
    "final marker list")
write(path, text)

print("septinum-v2-visual-pass: transformed 7 production files")
