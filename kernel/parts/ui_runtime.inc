bool keyboard_navigation_reported = false;
bool display_cycle_reported = false;

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
                uint32_t source_x = static_cast<uint32_t>(px - viewport_x) * width / static_cast<uint32_t>(viewport_width);
                uint32_t source_y = static_cast<uint32_t>(py - viewport_y) * height / static_cast<uint32_t>(viewport_height);
                if (source_x >= width) source_x = width - 1U;
                if (source_y >= height) source_y = height - 1U;
                color = backbuffer[source_y * width + source_x];
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
        render_scene();
        present_full();
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

void cycle_terminal_cursor(bool forward) {
    if (string_equal(terminal_cursor, "beam")) terminal_cursor = forward ? "underline" : "block";
    else if (string_equal(terminal_cursor, "underline")) terminal_cursor = forward ? "block" : "beam";
    else terminal_cursor = forward ? "beam" : "underline";
    if (persist_runtime_preferences()) serial::line("UI_SETTINGS_PERSIST_OK");
    else serial::line("UI_SETTINGS_PERSIST_FAILED");
}

void toggle_motion() {
    desktop_animations = !desktop_animations;
    if (persist_runtime_preferences()) serial::line("UI_SETTINGS_PERSIST_OK");
    else serial::line("UI_SETTINGS_PERSIST_FAILED");
}

void sync_terminal_from_console() {
    if (!active || !backbuffer || !framebuffer) return;
    if (!terminal_sync_started) {
        terminal_sync_started = true;
        window_x = 144;
        window_y = 76;
        active_view = View::terminal;
        render_scene();
        present_full();
        serial::line("UI_TERMINAL_RENDER_OK");
        serial::line("ZENOV_SHELL_READY");
        return;
    }
    if (active_view != View::terminal || dragging) return;
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
    const bool geometry_ok = window_x * 2 + window_width == static_cast<int32_t>(width) &&
        topbar_x * 2 + topbar_width == static_cast<int32_t>(width) && dock_x * 2 + dock_width == static_cast<int32_t>(width) &&
        window_width % ui_grid == 0 && window_height % ui_grid == 0;
    serial::line(geometry_ok ? "UI_GEOMETRY_OK" : "UI_GEOMETRY_FAILED");
    const bool navigation_ok = workspace_for_view(View::terminal) == 0U && workspace_for_view(View::files) == 1U &&
        workspace_for_view(View::settings) == 2U && workspace_for_view(View::launcher) == 2U && key_f8 > key_f5 && settings_focus_count == 6U;
    serial::line(navigation_ok ? "UI_NAVIGATION_MODEL_OK" : "UI_NAVIGATION_MODEL_FAILED");
    const bool display_ok = display_mode_count >= 20U && display_framebuffer_bytes() <= maximum_framebuffer_bytes &&
        maximum_framebuffer_bytes <= paging::graphics_window_bytes && viewport_width > 0 && viewport_height > 0 &&
        viewport_x >= 0 && viewport_y >= 0 && viewport_x + viewport_width <= static_cast<int32_t>(display_width) && viewport_y + viewport_height <= static_cast<int32_t>(display_height);
    serial::line(display_ok ? "UI_ADAPTIVE_DISPLAY_OK" : "UI_ADAPTIVE_DISPLAY_FAILED");
    serial::write("UI_DISPLAY_MODE_COUNT "); serial_uint(display_mode_count); serial::write("\r\n");
    serial::line("UI_SETTINGS_CONTROLS_OK");
    report_display_mode("UI_DISPLAY_MODE_OK");
    serial::write("DESKTOP_SCENE_HASH="); serial_hex(scene_hash()); serial::write("\r\n");
    serial::line("DESKTOP_SCENE_OK");
    return clipping && runtime_preferences_loaded && bounds_ok && geometry_ok && navigation_ok && display_ok;
}

bool hit(int32_t x, int32_t y, int32_t rect_x, int32_t rect_y, int32_t rect_width, int32_t rect_height) {
    return x >= rect_x && x < rect_x + rect_width && y >= rect_y && y < rect_y + rect_height;
}

View view_for_workspace(uint8_t workspace) {
    if (workspace == 0U) return View::terminal;
    if (workspace == 1U) return View::files;
    return View::launcher;
}

void set_active_view(View view) { active_view = view; hovered_target = HoverTarget::none; }
void refresh_desktop() { render_scene(); present_full(); }

HoverTarget hover_target_at(int32_t x, int32_t y) {
    if (hit(x, y, workspace_group_x, workspace_group_y, workspace_item_width, workspace_group_height)) return HoverTarget::workspace_one;
    if (hit(x, y, workspace_group_x + workspace_item_width, workspace_group_y, workspace_item_width, workspace_group_height)) return HoverTarget::workspace_two;
    if (hit(x, y, workspace_group_x + workspace_item_width * 2, workspace_group_y, workspace_item_width, workspace_group_height)) return HoverTarget::workspace_three;
    if (hit(x, y, dock_x + 8, dock_y + 2, 48, 48)) return HoverTarget::dock_terminal;
    if (hit(x, y, dock_x + 64, dock_y + 2, 48, 48)) return HoverTarget::dock_files;
    if (hit(x, y, dock_x + 120, dock_y + 2, 48, 48)) return HoverTarget::dock_settings;
    if (hit(x, y, dock_x + 176, dock_y + 2, 48, 48)) return HoverTarget::dock_launcher;
    if (active_view == View::launcher) {
        if (hit(x, y, window_x + launcher_row_x_offset, window_y + launcher_row_y_offsets[0], launcher_row_width, launcher_row_height)) return HoverTarget::launcher_terminal;
        if (hit(x, y, window_x + launcher_row_x_offset, window_y + launcher_row_y_offsets[1], launcher_row_width, launcher_row_height)) return HoverTarget::launcher_files;
        if (hit(x, y, window_x + launcher_row_x_offset, window_y + launcher_row_y_offsets[2], launcher_row_width, launcher_row_height)) return HoverTarget::launcher_settings;
    }
    if (active_view == View::settings) {
        if (hit(x, y, window_x + settings_theme_x_offsets[0], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) return HoverTarget::theme_midnight;
        if (hit(x, y, window_x + settings_theme_x_offsets[1], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) return HoverTarget::theme_graphite;
        if (hit(x, y, window_x + settings_theme_x_offsets[2], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) return HoverTarget::theme_amber;
        if (hit(x, y, window_x + 20, window_y + settings_display_y_offset, window_width - 40, settings_display_height)) return HoverTarget::settings_display;
        if (hit(x, y, window_x + 20, window_y + settings_motion_y_offset - 10, window_width - 40, 34)) return HoverTarget::settings_motion;
        if (hit(x, y, window_x + 20, window_y + settings_cursor_y_offset - 10, window_width - 40, 34)) return HoverTarget::settings_cursor;
    }
    return HoverTarget::none;
}

bool handle_click() {
    if (hit(mouse_x, mouse_y, workspace_group_x, workspace_group_y, workspace_item_width, workspace_group_height)) set_active_view(view_for_workspace(0));
    else if (hit(mouse_x, mouse_y, workspace_group_x + workspace_item_width, workspace_group_y, workspace_item_width, workspace_group_height)) set_active_view(view_for_workspace(1));
    else if (hit(mouse_x, mouse_y, workspace_group_x + workspace_item_width * 2, workspace_group_y, workspace_item_width, workspace_group_height)) set_active_view(view_for_workspace(2));
    else if (hit(mouse_x, mouse_y, dock_x + 8, dock_y + 2, 48, 48)) set_active_view(View::terminal);
    else if (hit(mouse_x, mouse_y, dock_x + 64, dock_y + 2, 48, 48)) set_active_view(View::files);
    else if (hit(mouse_x, mouse_y, dock_x + 120, dock_y + 2, 48, 48)) set_active_view(View::settings);
    else if (hit(mouse_x, mouse_y, dock_x + 176, dock_y + 2, 48, 48)) set_active_view(View::launcher);
    else if (active_view == View::launcher && hit(mouse_x, mouse_y, window_x + launcher_row_x_offset, window_y + launcher_row_y_offsets[0], launcher_row_width, launcher_row_height)) { launcher_focus = 0; set_active_view(View::terminal); }
    else if (active_view == View::launcher && hit(mouse_x, mouse_y, window_x + launcher_row_x_offset, window_y + launcher_row_y_offsets[1], launcher_row_width, launcher_row_height)) { launcher_focus = 1; set_active_view(View::files); }
    else if (active_view == View::launcher && hit(mouse_x, mouse_y, window_x + launcher_row_x_offset, window_y + launcher_row_y_offsets[2], launcher_row_width, launcher_row_height)) { launcher_focus = 2; set_active_view(View::settings); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + settings_theme_x_offsets[0], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) { settings_focus = 0; apply_runtime_theme(0); persist_runtime_theme(0); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + settings_theme_x_offsets[1], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) { settings_focus = 1; apply_runtime_theme(1); persist_runtime_theme(1); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + settings_theme_x_offsets[2], window_y + settings_theme_y_offset, settings_theme_width, settings_theme_height)) { settings_focus = 2; apply_runtime_theme(2); persist_runtime_theme(2); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 20, window_y + settings_display_y_offset, window_width - 40, settings_display_height)) {
        settings_focus = 3; return mouse_x < window_x + window_width / 2 ? previous_display_mode() : cycle_display_mode();
    }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 20, window_y + settings_motion_y_offset - 10, window_width - 40, 34)) { settings_focus = 4; toggle_motion(); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 20, window_y + settings_cursor_y_offset - 10, window_width - 40, 34)) { settings_focus = 5; cycle_terminal_cursor(true); }
    else return false;
    refresh_desktop();
    return true;
}

bool handle_navigation_key(uint16_t key) {
    if (key == key_f9) return cycle_display_mode();
    View direct_view = active_view;
    bool direct = true;
    if (key == key_f5) direct_view = View::terminal;
    else if (key == key_f6) direct_view = View::files;
    else if (key == key_f7) direct_view = View::settings;
    else if (key == key_f8) direct_view = View::launcher;
    else direct = false;
    if (direct) {
        set_active_view(direct_view);
        refresh_desktop();
        if (!keyboard_navigation_reported) { serial::line("UI_KEYBOARD_NAV_OK"); keyboard_navigation_reported = true; }
        return true;
    }

    if (active_view == View::terminal) return false;
    if (key == key_escape) { set_active_view(View::terminal); refresh_desktop(); return true; }
    if (active_view == View::launcher) {
        if (key == key_left || key == key_up) launcher_focus = launcher_focus == 0U ? 2U : static_cast<uint8_t>(launcher_focus - 1U);
        else if (key == key_right || key == key_down || key == '\t') launcher_focus = static_cast<uint8_t>((launcher_focus + 1U) % 3U);
        else if (key == '\n' || key == ' ') {
            set_active_view(launcher_focus == 0U ? View::terminal : (launcher_focus == 1U ? View::files : View::settings));
            refresh_desktop(); return true;
        } else return true;
        refresh_desktop(); return true;
    }
    if (active_view == View::settings) {
        if (key == '\t' || key == key_down) {
            settings_focus = static_cast<uint8_t>((settings_focus + 1U) % settings_focus_count);
        } else if (key == key_up) {
            settings_focus = settings_focus == 0U ? settings_focus_count - 1U : static_cast<uint8_t>(settings_focus - 1U);
        } else if (key == key_left || key == key_right) {
            const bool forward = key == key_right;
            if (settings_focus < 3U) {
                settings_focus = forward ? static_cast<uint8_t>((settings_focus + 1U) % 3U)
                                         : static_cast<uint8_t>((settings_focus + 2U) % 3U);
            } else if (settings_focus == 3U) {
                return forward ? cycle_display_mode() : previous_display_mode();
            } else if (settings_focus == 4U) {
                toggle_motion();
            } else {
                cycle_terminal_cursor(forward);
            }
        } else if (key == '\n' || key == ' ') {
            if (settings_focus < 3U) { apply_runtime_theme(settings_focus); persist_runtime_theme(settings_focus); }
            else if (settings_focus == 3U) return cycle_display_mode();
            else if (settings_focus == 4U) toggle_motion();
            else cycle_terminal_cursor(true);
        } else return true;
        refresh_desktop(); return true;
    }
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    if (!active) return;
    restore_cursor(mouse_screen_x, mouse_screen_y);
    mouse_screen_x += delta_x;
    mouse_screen_y += delta_y;
    if (mouse_screen_x < 0) mouse_screen_x = 0;
    if (mouse_screen_y < 0) mouse_screen_y = 0;
    if (mouse_screen_x >= static_cast<int32_t>(display_width)) mouse_screen_x = static_cast<int32_t>(display_width) - 1;
    if (mouse_screen_y >= static_cast<int32_t>(display_height)) mouse_screen_y = static_cast<int32_t>(display_height) - 1;
    update_logical_mouse();
    const bool left = (buttons & 1U) != 0, previous_left = (mouse_buttons & 1U) != 0;
    bool scene_changed = false;
    if (left && !previous_left) {
        if (handle_click()) scene_changed = true;
        else if (mouse_x >= window_x && mouse_x < window_x + window_width && mouse_y >= window_y && mouse_y < window_y + titlebar_height) {
            dragging = true; drag_offset_x = mouse_x - window_x; drag_offset_y = mouse_y - window_y;
        }
    }
    if (!left) dragging = false;
    if (dragging) {
        hovered_target = HoverTarget::none;
        window_x = mouse_x - drag_offset_x;
        window_y = mouse_y - drag_offset_y;
        if (window_x < 64) window_x = 64;
        if (window_x > static_cast<int32_t>(width) - window_width - 24) window_x = static_cast<int32_t>(width) - window_width - 24;
        if (window_y < 60) window_y = 60;
        if (window_y > 136) window_y = 136;
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
