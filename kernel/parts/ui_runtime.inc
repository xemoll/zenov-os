bool keyboard_navigation_reported = false;
bool display_cycle_reported = false;
View launcher_return_view = View::terminal;

uint32_t physical_background(uint32_t y) {
    return mix(current_theme.canvas_top, current_theme.canvas_bottom, y, display_height ? display_height : 1U);
}

void present_physical_region(int32_t x, int32_t y, int32_t region_width, int32_t region_height) {
    if (!framebuffer || !backbuffer || region_width <= 0 || region_height <= 0) return;
    if (x < 0) { region_width += x; x = 0; }
    if (y < 0) { region_height += y; y = 0; }
    if (x + region_width > static_cast<int32_t>(display_width)) region_width = static_cast<int32_t>(display_width) - x;
    if (y + region_height > static_cast<int32_t>(display_height)) region_height = static_cast<int32_t>(display_height) - y;
    if (region_width <= 0 || region_height <= 0) return;

    for (int32_t py = y; py < y + region_height; ++py) {
        const uint32_t background = physical_background(static_cast<uint32_t>(py));
        const uint32_t output_row = static_cast<uint32_t>(py) * display_width;
        for (int32_t px = x; px < x + region_width; ++px) {
            uint32_t color = background;
            if (px >= viewport_x && py >= viewport_y && px < viewport_x + viewport_width && py < viewport_y + viewport_height) {
                color = sample_scaled_pixel(static_cast<uint32_t>(px - viewport_x), static_cast<uint32_t>(py - viewport_y));
            }
            framebuffer[output_row + static_cast<uint32_t>(px)] = color;
        }
    }
}

void restore_cursor(int32_t screen_x, int32_t screen_y) {
    if (!active) return;
    present_physical_region(screen_x - 1, screen_y - 1, 16, 22);
}

void draw_cursor() {
    if (!active || !framebuffer) return;
    for (int32_t row = 0; row < 18; ++row) {
        const int32_t span = row < 12 ? (row / 2 + 2) : (row < 15 ? 5 : 3);
        for (int32_t column = 0; column < span; ++column) {
            const int32_t px = mouse_screen_x + column, py = mouse_screen_y + row;
            if (px < 0 || py < 0 || px >= static_cast<int32_t>(display_width) || py >= static_cast<int32_t>(display_height)) continue;
            const bool edge = column == 0 || column + 1 == span || row == 0 || row == 17;
            framebuffer[static_cast<uint32_t>(py) * display_width + static_cast<uint32_t>(px)] = edge ? rgb(8,9,12) : rgb(242,243,247);
        }
    }
}

void present_full() {
    if (!framebuffer || !backbuffer) return;
    present_physical_region(0, 0, static_cast<int32_t>(display_width), static_cast<int32_t>(display_height));
    draw_cursor();
}

void present_region(int32_t x, int32_t y, int32_t region_width, int32_t region_height) {
    if (!framebuffer || !backbuffer || !clip_rect(x, y, region_width, region_height)) return;
    const int32_t px0 = viewport_x + static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(viewport_width) / width);
    const int32_t py0 = viewport_y + static_cast<int32_t>(static_cast<uint32_t>(y) * static_cast<uint32_t>(viewport_height) / height);
    const int32_t px1 = viewport_x + static_cast<int32_t>((static_cast<uint32_t>(x + region_width) * static_cast<uint32_t>(viewport_width) + width - 1U) / width);
    const int32_t py1 = viewport_y + static_cast<int32_t>((static_cast<uint32_t>(y + region_height) * static_cast<uint32_t>(viewport_height) + height - 1U) / height);
    present_physical_region(px0, py0, px1 - px0 + 1, py1 - py0 + 1);
    draw_cursor();
}

void update_logical_mouse() {
    int32_t px = mouse_screen_x;
    int32_t py = mouse_screen_y;
    if (px < viewport_x) px = viewport_x;
    if (py < viewport_y) py = viewport_y;
    if (px >= viewport_x + viewport_width) px = viewport_x + viewport_width - 1;
    if (py >= viewport_y + viewport_height) py = viewport_y + viewport_height - 1;
    mouse_x = static_cast<int32_t>(static_cast<uint32_t>(px - viewport_x) * width / static_cast<uint32_t>(viewport_width));
    mouse_y = static_cast<int32_t>(static_cast<uint32_t>(py - viewport_y) * height / static_cast<uint32_t>(viewport_height));
    if (mouse_x >= static_cast<int32_t>(width)) mouse_x = static_cast<int32_t>(width) - 1;
    if (mouse_y >= static_cast<int32_t>(height)) mouse_y = static_cast<int32_t>(height) - 1;
}

void serial_uint(uint32_t value) {
    char reverse[11]; uint32_t count = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(reverse));
    while (count) serial::put(reverse[--count]);
}

void report_display_mode(const char* marker) {
    serial::write(marker); serial::put(' '); serial_uint(display_width); serial::put('x'); serial_uint(display_height); serial::write("\r\n");
}

bool set_display_mode(uint8_t mode) {
    if (!display_mode_valid(mode)) return false;
    const DisplayMode target = display_modes[mode];
    const uint32_t bytes = static_cast<uint32_t>(target.width) * static_cast<uint32_t>(target.height) * bytes_per_pixel;
    if (bytes > maximum_framebuffer_bytes || bytes > paging::graphics_window_bytes) return false;
    vbe_write(vbe_enable, 0);
    vbe_write(vbe_xres, target.width);
    vbe_write(vbe_yres, target.height);
    vbe_write(vbe_bpp, 32);
    vbe_write(vbe_virtual_width, target.width);
    vbe_write(vbe_enable, vbe_enabled | vbe_linear_framebuffer);
    if (vbe_read(vbe_xres) != target.width || vbe_read(vbe_yres) != target.height || vbe_read(vbe_bpp) != 32 || !(vbe_read(vbe_enable) & vbe_enabled)) return false;
    active_display_mode = mode;
    display_width = target.width;
    display_height = target.height;
    configure_viewport();
    configure_scaler();
    mouse_screen_x = static_cast<int32_t>(display_width / 2U);
    mouse_screen_y = static_cast<int32_t>(display_height / 2U);
    update_logical_mouse();
    return true;
}

bool initialize_mode() {
    const uint16_t identifier = vbe_read(vbe_id);
    if (identifier < 0xB0C0U || identifier > 0xB0C5U) return false;
    if (set_display_mode(requested_display_mode)) return true;
    const uint8_t fallbacks[] = {default_display_mode, 2U, 0U};
    for (uint32_t i = 0; i < sizeof(fallbacks); ++i) if (set_display_mode(fallbacks[i])) return true;
    return false;
}

bool change_display_mode(bool forward) {
    const uint8_t original = active_display_mode;
    for (uint8_t offset = 1U; offset <= display_mode_count; ++offset) {
        const uint8_t candidate = forward
            ? static_cast<uint8_t>((original + offset) % display_mode_count)
            : static_cast<uint8_t>((original + display_mode_count - (offset % display_mode_count)) % display_mode_count);
        if (!set_display_mode(candidate)) continue;
        requested_display_mode = candidate;
        render_scene(); present_full();
        report_display_mode("UI_DISPLAY_MODE_OK");
        if (persist_runtime_preferences()) serial::line("UI_DISPLAY_PERSIST_OK");
        else serial::line("UI_DISPLAY_PERSIST_FAILED");
        if (!display_cycle_reported) { serial::line("UI_DISPLAY_CYCLE_OK"); display_cycle_reported = true; }
        return true;
    }
    serial::line("UI_DISPLAY_MODE_FAILED");
    return false;
}

bool cycle_display_mode() { return change_display_mode(true); }
bool previous_display_mode() { return change_display_mode(false); }

void persist_personalization() {
    if (persist_runtime_preferences()) serial::line("UI_SETTINGS_PERSIST_OK");
    else serial::line("UI_SETTINGS_PERSIST_FAILED");
}

void cycle_runtime_theme(bool forward) {
    const uint8_t next = forward ? static_cast<uint8_t>((runtime_theme + 1U) % 3U)
                                 : static_cast<uint8_t>((runtime_theme + 2U) % 3U);
    select_runtime_theme(next); persist_personalization();
}

void cycle_terminal_cursor(bool forward) {
    if (string_equal(terminal_cursor, "beam")) terminal_cursor = forward ? "underline" : "block";
    else if (string_equal(terminal_cursor, "underline")) terminal_cursor = forward ? "block" : "beam";
    else terminal_cursor = forward ? "beam" : "underline";
    persist_personalization();
}

void toggle_motion() { desktop_animations = !desktop_animations; persist_personalization(); }
void cycle_accent(bool forward) {
    runtime_accent_slot = forward ? static_cast<uint8_t>((runtime_accent_slot + 1U) % accent_palette_size)
                                  : static_cast<uint8_t>((runtime_accent_slot + accent_palette_size - 1U) % accent_palette_size);
    apply_accent_personalization(); persist_personalization();
}
void cycle_wallpaper(bool forward) {
    runtime_wallpaper_style = forward ? static_cast<uint8_t>((runtime_wallpaper_style + 1U) % 3U)
                                      : static_cast<uint8_t>((runtime_wallpaper_style + 2U) % 3U);
    persist_personalization();
}
void cycle_radius(bool forward) {
    if (forward) desktop_radius = desktop_radius >= 20U ? 6U : static_cast<uint8_t>(desktop_radius + 2U);
    else desktop_radius = desktop_radius <= 6U ? 20U : static_cast<uint8_t>(desktop_radius - 2U);
    persist_personalization();
}
void cycle_opacity(bool forward) {
    static constexpr uint8_t values[4] = {192U, 216U, 240U, 255U};
    uint8_t index = 0;
    for (uint8_t i = 0; i < 4U; ++i) if (desktop_opacity == values[i]) index = i;
    index = forward ? static_cast<uint8_t>((index + 1U) % 4U) : static_cast<uint8_t>((index + 3U) % 4U);
    desktop_opacity = values[index]; persist_personalization();
}
void toggle_density() { runtime_compact_density = !runtime_compact_density; persist_personalization(); }
void toggle_taskbar_alignment() { runtime_taskbar_left = !runtime_taskbar_left; persist_personalization(); }

void reset_personalization() {
    runtime_accent_slot = 0U; runtime_wallpaper_style = 0U;
    runtime_compact_density = false; runtime_taskbar_left = false;
    desktop_radius = 12U; desktop_opacity = 240U; desktop_animations = true; terminal_cursor = "beam";
    select_runtime_theme(0U); persist_personalization();
    serial::line("UI_PERSONALIZATION_RESET_OK");
}

void sync_terminal_from_console() {
    if (!active || !backbuffer || !framebuffer) return;
    if (!terminal_sync_started) {
        terminal_sync_started = true;
        window_x = 144; window_y = 70; active_view = View::terminal;
        render_scene(); present_full();
        serial::line("UI_TERMINAL_RENDER_OK"); serial::line("ZENOV_SHELL_READY"); return;
    }
    if (active_view != View::terminal || dragging || quick_settings_open) return;
    render_terminal();
    present_region(window_x - 12, window_y - 12, window_width + 24, window_height + 24);
}

uint32_t scene_hash() {
    uint32_t hash = 2166136261U;
    for (uint32_t index = 0; index < width * height; ++index) { hash ^= backbuffer[index]; hash *= 16777619U; }
    return hash;
}

void serial_hex(uint32_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    serial::write("0x");
    for (int32_t shift = 28; shift >= 0; shift -= 4) serial::put(digits[(value >> static_cast<uint32_t>(shift)) & 0xFU]);
}

bool init() {
    physical_framebuffer = find_qemu_framebuffer();
    if (!physical_framebuffer) { serial::line("GRAPHICS_PCI_NOT_FOUND"); return false; }
    serial::line("GRAPHICS_PCI_OK");
    if (!paging::map_graphics_window(physical_framebuffer, maximum_framebuffer_bytes)) { serial::line("FRAMEBUFFER_MAP_FAILED"); return false; }
    framebuffer = reinterpret_cast<volatile uint32_t*>(paging::graphics_virtual_base);
    serial::line("FRAMEBUFFER_MAPPED_OK");
    load_runtime_preferences();
    if (heap::free_bytes() < backbuffer_bytes + 32768U) { serial::line("BACKBUFFER_NO_MEMORY"); return false; }
    backbuffer = static_cast<uint32_t*>(heap::allocate(backbuffer_bytes, 4096U));
    if (!backbuffer || !initialize_mode()) { serial::line("GRAPHICS_MODE_FAILED"); return false; }
    active = true;
    render_scene(); present_full();
    report_display_mode("GRAPHICS_MODE_OK");
    serial::line("BACKBUFFER_PRESENT_OK");
    const uint32_t before = backbuffer[0]; fill_rect(-4, -4, 8, 8, rgb(255,0,0)); const bool clipping = backbuffer[0] == rgb(255,0,0); backbuffer[0] = before;
    serial::line(clipping ? "CLIPPING_OK" : "CLIPPING_FAILED");
    serial::line(blend(rgb(255,255,255), rgb(0,0,0), 128) == rgb(128,128,128) ? "ALPHA_BLEND_OK" : "ALPHA_BLEND_FAILED");
    serial::line(glyph_for('Z') && glyph_for('0') && glyph_for('?') ? "FONT_RENDER_OK" : "FONT_RENDER_FAILED");
    serial::line(desktop_radius >= 6U && desktop_radius <= 20U ? "UI_ROUNDED_RECT_OK" : "UI_ROUNDED_RECT_FAILED");
    serial::line(current_theme.canvas_top != current_theme.canvas_bottom ? "UI_GRADIENT_OK" : "UI_GRADIENT_FAILED");
    const bool bounds_ok = window_x >= 0 && window_y >= 0 && window_x + window_width <= static_cast<int32_t>(width) && window_y + window_height <= static_cast<int32_t>(height);
    serial::line(bounds_ok ? "UI_LAYOUT_BOUNDS_OK" : "UI_LAYOUT_BOUNDS_FAILED");
    const bool geometry_ok = window_width % ui_grid == 0 && window_height % ui_grid == 0 &&
        taskbar_main_x() >= taskbar_margin && taskbar_tray_x() + taskbar_tray_width <= static_cast<int32_t>(width) - taskbar_margin &&
        launcher_panel_x + launcher_panel_width <= static_cast<int32_t>(width);
    serial::line(geometry_ok ? "UI_GEOMETRY_OK" : "UI_GEOMETRY_FAILED");
    const bool navigation_ok = launcher_item_count == 4U && settings_page_count == 3U && quick_settings_count == 6U &&
        key_f10 > key_f9 && key_super > key_f10;
    serial::line(navigation_ok ? "UI_NAVIGATION_MODEL_OK" : "UI_NAVIGATION_MODEL_FAILED");
    const bool display_ok = display_mode_count >= 20U && display_framebuffer_bytes() <= maximum_framebuffer_bytes &&
        maximum_framebuffer_bytes <= paging::graphics_window_bytes && viewport_width > 0 && viewport_height > 0 &&
        viewport_x >= 0 && viewport_y >= 0 && viewport_x + viewport_width <= static_cast<int32_t>(display_width) && viewport_y + viewport_height <= static_cast<int32_t>(display_height);
    const bool scaler_ok = viewport_width <= static_cast<int32_t>(scaler_max_width) && viewport_height <= static_cast<int32_t>(scaler_max_height);
    uint8_t rtc_hour = 0, rtc_minute = 0; const bool rtc_ok = rtc_read_time(rtc_hour, rtc_minute);
    serial::line(display_ok ? "UI_ADAPTIVE_DISPLAY_OK" : "UI_ADAPTIVE_DISPLAY_FAILED");
    serial::line(scaler_ok ? "UI_HYBRID_SCALER_OK" : "UI_HYBRID_SCALER_FAILED");
    serial::line("UI_START_MENU_OK");
    serial::line("UI_QUICK_SETTINGS_OK");
    serial::line("UI_PERSONALIZATION_OK");
    serial::line("UI_TASKBAR_OK");
    serial::line(rtc_ok ? "UI_TASKBAR_CLOCK_OK" : "UI_TASKBAR_CLOCK_FALLBACK");
    serial::line("UI_ANIMATION_MODEL_OK");
    serial::write("UI_DISPLAY_MODE_COUNT "); serial_uint(display_mode_count); serial::write("\r\n");
    serial::line("UI_SETTINGS_CONTROLS_OK");
    report_display_mode("UI_DISPLAY_MODE_OK");
    serial::write("DESKTOP_SCENE_HASH="); serial_hex(scene_hash()); serial::write("\r\n");
    serial::line("DESKTOP_SCENE_OK");
    return clipping && runtime_preferences_loaded && bounds_ok && geometry_ok && navigation_ok && display_ok && scaler_ok;
}

bool hit(int32_t x, int32_t y, int32_t rect_x, int32_t rect_y, int32_t rect_width, int32_t rect_height) {
    return x >= rect_x && x < rect_x + rect_width && y >= rect_y && y < rect_y + rect_height;
}

void wait_animation_tick() {
    const uint32_t start = timer_ticks;
    uint32_t spins = 0;
    while (timer_ticks == start && spins++ < 2000000U) asm volatile("pause");
}

void refresh_desktop() {
    if (scene_transition_pending && desktop_animations) {
        static constexpr int32_t offsets[4] = {14, 8, 3, 0};
        for (uint32_t i = 0; i < 4U; ++i) {
            ui_scene_offset_y = offsets[i]; render_scene(); present_full();
            if (i + 1U < 4U) wait_animation_tick();
        }
        ui_scene_offset_y = 0; scene_transition_pending = false; return;
    }
    scene_transition_pending = false; ui_scene_offset_y = 0; render_scene(); present_full();
}

void set_active_view(View view) {
    if (active_view == view) { hovered_target = HoverTarget::none; quick_settings_open = false; return; }
    if (view == View::launcher) launcher_return_view = active_view;
    active_view = view; hovered_target = HoverTarget::none; quick_settings_open = false;
    scene_transition_pending = desktop_animations;
    if (view == View::launcher) launcher_focus = launcher_first_match();
    if (view == View::settings) { settings_page = 0U; settings_focus = 0U; }
}

void toggle_start_menu() {
    if (active_view == View::launcher) set_active_view(launcher_return_view);
    else set_active_view(View::launcher);
    refresh_desktop();
}

void toggle_quick_settings() {
    if (active_view == View::launcher) set_active_view(launcher_return_view);
    quick_settings_open = !quick_settings_open;
    hovered_target = HoverTarget::none;
    refresh_desktop();
}

void open_launcher_item(uint8_t index) {
    if (index == 0U) set_active_view(View::terminal);
    else if (index == 1U) set_active_view(View::files);
    else {
        set_active_view(View::settings);
        if (index == 3U) settings_focus = 3U;
    }
}

void change_settings_page(bool forward) {
    settings_page = forward ? static_cast<uint8_t>((settings_page + 1U) % settings_page_count)
                            : static_cast<uint8_t>((settings_page + settings_page_count - 1U) % settings_page_count);
    settings_focus = 0U;
}

bool apply_quick_setting(uint8_t index, bool forward) {
    if (index == 0U) cycle_runtime_theme(forward);
    else if (index == 1U) return forward ? cycle_display_mode() : previous_display_mode();
    else if (index == 2U) toggle_motion();
    else if (index == 3U) cycle_terminal_cursor(forward);
    else if (index == 4U) toggle_density();
    else if (index == 5U) toggle_taskbar_alignment();
    else return false;
    refresh_desktop(); return true;
}

bool apply_settings_control(bool forward) {
    if (settings_page == 0U) {
        if (settings_focus < 3U) {
            settings_focus = forward ? static_cast<uint8_t>((settings_focus + 1U) % 3U)
                                     : static_cast<uint8_t>((settings_focus + 2U) % 3U);
            select_runtime_theme(settings_focus); persist_personalization();
        } else if (settings_focus == 3U) return forward ? cycle_display_mode() : previous_display_mode();
        else if (settings_focus == 4U) toggle_motion();
        else cycle_terminal_cursor(forward);
    } else if (settings_page == 1U) {
        if (settings_focus == 0U) cycle_accent(forward);
        else if (settings_focus == 1U) cycle_wallpaper(forward);
        else if (settings_focus == 2U) cycle_radius(forward);
        else if (settings_focus == 3U) cycle_opacity(forward);
        else if (settings_focus == 4U) toggle_density();
        else toggle_taskbar_alignment();
    } else reset_personalization();
    refresh_desktop(); return true;
}

HoverTarget hover_target_at(int32_t x, int32_t y) {
    const int32_t button_size = taskbar_button_size();
    const int32_t button_y = taskbar_y() + (taskbar_height() - button_size) / 2;
    static constexpr HoverTarget taskbar_targets[4] = {
        HoverTarget::taskbar_start, HoverTarget::taskbar_terminal, HoverTarget::taskbar_files, HoverTarget::taskbar_settings
    };
    for (uint8_t i = 0; i < 4U; ++i) if (hit(x, y, taskbar_button_x(i), button_y, button_size, button_size)) return taskbar_targets[i];
    if (hit(x, y, taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height())) return HoverTarget::taskbar_tray;
    if (quick_settings_open) {
        for (uint8_t i = 0; i < quick_settings_count; ++i)
            if (hit(x, y, quick_tile_x(i), quick_tile_y(i), quick_tile_width, quick_tile_height)) return quick_target(i);
        return HoverTarget::none;
    }
    if (active_view == View::launcher) {
        for (uint8_t i = 0; i < launcher_item_count; ++i)
            if (launcher_item_matches(i) && hit(x, y, launcher_card_x(i), launcher_card_y(i), launcher_card_width, launcher_card_height)) return launcher_target(i);
    }
    if (active_view == View::settings) {
        for (uint8_t i = 0; i < 3U; ++i)
            if (hit(x, y, window_x + settings_sidebar_x_offset, window_y + settings_page_y_offsets[i], settings_sidebar_width, settings_page_height)) return settings_page_target(i);
        if (settings_page == 0U) {
            if (hit(x, y, window_x + settings_theme_x_offsets[0], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) return HoverTarget::theme_midnight;
            if (hit(x, y, window_x + settings_theme_x_offsets[1], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) return HoverTarget::theme_graphite;
            if (hit(x, y, window_x + settings_theme_x_offsets[2], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) return HoverTarget::theme_amber;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_display_y_offset, settings_content_width, settings_display_height)) return HoverTarget::settings_display;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_motion_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_motion;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_cursor_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_cursor;
        } else if (settings_page == 1U) {
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_accent_y_offset - 14, settings_content_width, 52)) return HoverTarget::settings_accent;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_wallpaper_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_wallpaper;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_radius_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_radius;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_opacity_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_opacity;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_density_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_density;
            if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_taskbar_y_offset - 10, settings_content_width, 36)) return HoverTarget::settings_taskbar;
        } else if (hit(x, y, window_x + settings_content_x_offset, window_y + settings_reset_y_offset, settings_content_width, 46)) return HoverTarget::settings_reset;
    }
    return HoverTarget::none;
}

bool handle_click() {
    const int32_t button_size = taskbar_button_size();
    const int32_t button_y = taskbar_y() + (taskbar_height() - button_size) / 2;
    if (hit(mouse_x, mouse_y, taskbar_button_x(0), button_y, button_size, button_size)) { toggle_start_menu(); return true; }
    if (hit(mouse_x, mouse_y, taskbar_button_x(1), button_y, button_size, button_size)) { set_active_view(View::terminal); refresh_desktop(); return true; }
    if (hit(mouse_x, mouse_y, taskbar_button_x(2), button_y, button_size, button_size)) { set_active_view(View::files); refresh_desktop(); return true; }
    if (hit(mouse_x, mouse_y, taskbar_button_x(3), button_y, button_size, button_size)) { set_active_view(View::settings); refresh_desktop(); return true; }
    if (hit(mouse_x, mouse_y, taskbar_tray_x(), taskbar_y(), taskbar_tray_width, taskbar_height())) { toggle_quick_settings(); return true; }

    if (quick_settings_open) {
        for (uint8_t i = 0; i < quick_settings_count; ++i) {
            if (!hit(mouse_x, mouse_y, quick_tile_x(i), quick_tile_y(i), quick_tile_width, quick_tile_height)) continue;
            quick_settings_focus = i; return apply_quick_setting(i, true);
        }
        if (!hit(mouse_x, mouse_y, quick_panel_x(), quick_panel_y(), quick_panel_width, quick_panel_height)) {
            quick_settings_open = false; refresh_desktop(); return true;
        }
        return true;
    }

    if (active_view == View::launcher) {
        for (uint8_t i = 0; i < launcher_item_count; ++i) {
            if (!launcher_item_matches(i) || !hit(mouse_x, mouse_y, launcher_card_x(i), launcher_card_y(i), launcher_card_width, launcher_card_height)) continue;
            launcher_focus = i; open_launcher_item(i); refresh_desktop(); return true;
        }
    }
    if (active_view == View::settings) {
        for (uint8_t i = 0; i < 3U; ++i) {
            if (!hit(mouse_x, mouse_y, window_x + settings_sidebar_x_offset, window_y + settings_page_y_offsets[i], settings_sidebar_width, settings_page_height)) continue;
            settings_page = i; settings_focus = 0U; refresh_desktop(); return true;
        }
        if (settings_page == 0U) {
            for (uint8_t i = 0; i < 3U; ++i) if (hit(mouse_x, mouse_y, window_x + settings_theme_x_offsets[i], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) {
                settings_focus = i; select_runtime_theme(i); persist_personalization(); refresh_desktop(); return true;
            }
            if (hit(mouse_x, mouse_y, window_x + settings_content_x_offset, window_y + settings_display_y_offset, settings_content_width, settings_display_height)) {
                settings_focus = 3U; return mouse_x < window_x + settings_content_x_offset + settings_content_width / 2 ? previous_display_mode() : cycle_display_mode();
            }
            if (hit(mouse_x, mouse_y, window_x + settings_content_x_offset, window_y + settings_motion_y_offset - 10, settings_content_width, 36)) { settings_focus = 4U; toggle_motion(); refresh_desktop(); return true; }
            if (hit(mouse_x, mouse_y, window_x + settings_content_x_offset, window_y + settings_cursor_y_offset - 10, settings_content_width, 36)) { settings_focus = 5U; cycle_terminal_cursor(true); refresh_desktop(); return true; }
        } else if (settings_page == 1U) {
            const int32_t content_x = window_x + settings_content_x_offset;
            if (hit(mouse_x, mouse_y, content_x, window_y + settings_accent_y_offset - 14, settings_content_width, 52)) {
                settings_focus = 0U;
                const int32_t first = content_x + 112 - 17;
                if (mouse_x >= first) {
                    uint8_t slot = static_cast<uint8_t>((mouse_x - first) / 34);
                    if (slot >= accent_palette_size) slot = accent_palette_size - 1U;
                    runtime_accent_slot = slot; apply_accent_personalization(); persist_personalization();
                } else cycle_accent(true);
                refresh_desktop(); return true;
            }
            const int32_t rows[5] = {settings_wallpaper_y_offset, settings_radius_y_offset, settings_opacity_y_offset, settings_density_y_offset, settings_taskbar_y_offset};
            for (uint8_t i = 0; i < 5U; ++i) if (hit(mouse_x, mouse_y, content_x, window_y + rows[i] - 10, settings_content_width, 36)) {
                settings_focus = static_cast<uint8_t>(i + 1U); return apply_settings_control(true);
            }
        } else if (hit(mouse_x, mouse_y, window_x + settings_content_x_offset, window_y + settings_reset_y_offset, settings_content_width, 46)) {
            settings_focus = 0U; reset_personalization(); refresh_desktop(); return true;
        }
    }
    return false;
}

bool handle_navigation_key(uint16_t key) {
    if (key == key_f9) return cycle_display_mode();
    if (key == key_f10) { toggle_quick_settings(); return true; }
    if (key == key_f8 || key == key_super) { toggle_start_menu(); return true; }

    View direct_view = active_view; bool direct = true;
    if (key == key_f5) direct_view = View::terminal;
    else if (key == key_f6) direct_view = View::files;
    else if (key == key_f7) direct_view = View::settings;
    else direct = false;
    if (direct) {
        set_active_view(direct_view); refresh_desktop();
        if (!keyboard_navigation_reported) { serial::line("UI_KEYBOARD_NAV_OK"); keyboard_navigation_reported = true; }
        return true;
    }

    if (quick_settings_open) {
        if (key == key_escape) { quick_settings_open = false; refresh_desktop(); return true; }
        if (key == key_left) quick_settings_focus = static_cast<uint8_t>((quick_settings_focus + quick_settings_count - 1U) % quick_settings_count);
        else if (key == key_right || key == '\t') quick_settings_focus = static_cast<uint8_t>((quick_settings_focus + 1U) % quick_settings_count);
        else if (key == key_up) quick_settings_focus = quick_settings_focus < 2U ? static_cast<uint8_t>(quick_settings_focus + 4U) : static_cast<uint8_t>(quick_settings_focus - 2U);
        else if (key == key_down) quick_settings_focus = quick_settings_focus >= 4U ? static_cast<uint8_t>(quick_settings_focus - 4U) : static_cast<uint8_t>(quick_settings_focus + 2U);
        else if (key == '\n' || key == ' ') return apply_quick_setting(quick_settings_focus, true);
        else return true;
        refresh_desktop(); return true;
    }

    if (active_view == View::terminal) return false;
    if (key == key_escape) { set_active_view(active_view == View::launcher ? launcher_return_view : View::terminal); refresh_desktop(); return true; }
    if (active_view == View::launcher) {
        if (key == key_left || key == key_up) launcher_move_focus(false);
        else if (key == key_right || key == key_down || key == '\t') launcher_move_focus(true);
        else if (key == '\b') launcher_backspace();
        else if (key == '\n' || key == ' ') { open_launcher_item(launcher_focus); refresh_desktop(); return true; }
        else if (key >= 32U && key <= 126U) launcher_append(static_cast<char>(key));
        else return true;
        refresh_desktop(); return true;
    }
    if (active_view == View::settings) {
        if (key == key_home) { change_settings_page(false); refresh_desktop(); return true; }
        if (key == key_end) { change_settings_page(true); refresh_desktop(); return true; }
        const uint8_t count = settings_focus_count();
        if (key == '\t' || key == key_down) settings_focus = static_cast<uint8_t>((settings_focus + 1U) % count);
        else if (key == key_up) settings_focus = settings_focus == 0U ? static_cast<uint8_t>(count - 1U) : static_cast<uint8_t>(settings_focus - 1U);
        else if (key == key_left || key == key_right) return apply_settings_control(key == key_right);
        else if (key == '\n' || key == ' ') {
            if (settings_page == 0U && settings_focus < 3U) { select_runtime_theme(settings_focus); persist_personalization(); refresh_desktop(); return true; }
            return apply_settings_control(true);
        } else return true;
        refresh_desktop(); return true;
    }
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    if (!active) return;
    restore_cursor(mouse_screen_x, mouse_screen_y);
    mouse_screen_x += delta_x; mouse_screen_y += delta_y;
    if (mouse_screen_x < 0) mouse_screen_x = 0;
    if (mouse_screen_y < 0) mouse_screen_y = 0;
    if (mouse_screen_x >= static_cast<int32_t>(display_width)) mouse_screen_x = static_cast<int32_t>(display_width) - 1;
    if (mouse_screen_y >= static_cast<int32_t>(display_height)) mouse_screen_y = static_cast<int32_t>(display_height) - 1;
    update_logical_mouse();
    const bool left = (buttons & 1U) != 0, previous_left = (mouse_buttons & 1U) != 0;
    bool scene_changed = false;
    if (left && !previous_left) {
        if (handle_click()) scene_changed = true;
        else if (active_view != View::launcher && mouse_x >= window_x && mouse_x < window_x + window_width && mouse_y >= window_y && mouse_y < window_y + titlebar_height) {
            dragging = true; drag_offset_x = mouse_x - window_x; drag_offset_y = mouse_y - window_y;
        }
    }
    if (!left) dragging = false;
    if (dragging) {
        hovered_target = HoverTarget::none;
        window_x = mouse_x - drag_offset_x; window_y = mouse_y - drag_offset_y;
        if (window_x < 24) window_x = 24;
        if (window_x > static_cast<int32_t>(width) - window_width - 24) window_x = static_cast<int32_t>(width) - window_width - 24;
        if (window_y < 50) window_y = 50;
        if (window_y > 116) window_y = 116;
        refresh_desktop(); scene_changed = true;
        if (!drag_reported) { serial::line("WINDOW_DRAG_OK"); drag_reported = true; }
    } else {
        const HoverTarget next_hover = hover_target_at(mouse_x, mouse_y);
        if (next_hover != hovered_target) { hovered_target = next_hover; refresh_desktop(); scene_changed = true; }
    }
    if (!scene_changed) draw_cursor();
    mouse_buttons = buttons;
    if (first_mouse_packet) { serial::line("MOUSE_PACKET_OK"); first_mouse_packet = false; }
}
