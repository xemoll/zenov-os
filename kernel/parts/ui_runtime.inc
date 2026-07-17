bool keyboard_navigation_reported = false;

void restore_cursor(int32_t x, int32_t y) {
    if (!active || !backbuffer || !framebuffer) return;
    for (int32_t row = 0; row < 20; ++row) for (int32_t column = 0; column < 14; ++column) {
        const int32_t px = x + column, py = y + row;
        if (px >= 0 && py >= 0 && px < static_cast<int32_t>(width) && py < static_cast<int32_t>(height)) framebuffer[static_cast<uint32_t>(py) * width + static_cast<uint32_t>(px)] = backbuffer[static_cast<uint32_t>(py) * width + static_cast<uint32_t>(px)];
    }
}

void draw_cursor() {
    if (!active || !framebuffer) return;
    for (int32_t row = 0; row < 18; ++row) {
        const int32_t span = row < 12 ? (row / 2 + 2) : (row < 15 ? 5 : 3);
        for (int32_t column = 0; column < span; ++column) {
            const int32_t px = mouse_x + column, py = mouse_y + row;
            if (px < 0 || py < 0 || px >= static_cast<int32_t>(width) || py >= static_cast<int32_t>(height)) continue;
            const bool edge = column == 0 || column + 1 == span || row == 0 || row == 17;
            framebuffer[static_cast<uint32_t>(py) * width + static_cast<uint32_t>(px)] = edge ? rgb(7,9,16) : rgb(248,249,255);
        }
    }
}

void present_full() {
    if (!framebuffer || !backbuffer) return;
    for (uint32_t index = 0; index < width * height; ++index) framebuffer[index] = backbuffer[index];
    draw_cursor();
}

void present_region(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height) {
    if (!framebuffer || !backbuffer || !clip_rect(x, y, rect_width, rect_height)) return;
    for (int32_t row = 0; row < rect_height; ++row) {
        const uint32_t offset = static_cast<uint32_t>(y + row) * width + static_cast<uint32_t>(x);
        for (int32_t column = 0; column < rect_width; ++column) framebuffer[offset + static_cast<uint32_t>(column)] = backbuffer[offset + static_cast<uint32_t>(column)];
    }
    draw_cursor();
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

bool initialize_mode() {
    const uint16_t identifier = vbe_read(vbe_id);
    if (identifier < 0xB0C0U || identifier > 0xB0C5U) return false;
    vbe_write(vbe_enable, 0);
    vbe_write(vbe_xres, static_cast<uint16_t>(width));
    vbe_write(vbe_yres, static_cast<uint16_t>(height));
    vbe_write(vbe_bpp, 32);
    vbe_write(vbe_virtual_width, static_cast<uint16_t>(width));
    vbe_write(vbe_enable, vbe_enabled | vbe_linear_framebuffer);
    return vbe_read(vbe_xres) == width && vbe_read(vbe_yres) == height && vbe_read(vbe_bpp) == 32 && (vbe_read(vbe_enable) & vbe_enabled);
}

bool init() {
    physical_framebuffer = find_qemu_framebuffer();
    if (!physical_framebuffer) { serial::line("GRAPHICS_PCI_NOT_FOUND"); return false; }
    serial::line("GRAPHICS_PCI_OK");
    if (!paging::map_graphics_window(physical_framebuffer, framebuffer_bytes)) { serial::line("FRAMEBUFFER_MAP_FAILED"); return false; }
    framebuffer = reinterpret_cast<volatile uint32_t*>(paging::graphics_virtual_base);
    serial::line("FRAMEBUFFER_MAPPED_OK");
    if (heap::free_bytes() < framebuffer_bytes + 32768U) { serial::line("BACKBUFFER_NO_MEMORY"); return false; }
    backbuffer = static_cast<uint32_t*>(heap::allocate(framebuffer_bytes, 4096U));
    if (!backbuffer || !initialize_mode()) { serial::line("GRAPHICS_MODE_FAILED"); return false; }
    load_runtime_preferences();
    active = true;
    render_scene(); present_full();
    serial::line("GRAPHICS_MODE_OK 800x600x32");
    serial::line("BACKBUFFER_PRESENT_OK");
    const uint32_t before = backbuffer[0]; fill_rect(-4, -4, 8, 8, rgb(255,0,0)); const bool clipping = backbuffer[0] == rgb(255,0,0); backbuffer[0] = before;
    serial::line(clipping ? "CLIPPING_OK" : "CLIPPING_FAILED");
    serial::line(blend(rgb(255,255,255), rgb(0,0,0), 128) == rgb(128,128,128) ? "ALPHA_BLEND_OK" : "ALPHA_BLEND_FAILED");
    serial::line(glyph_for('Z') && glyph_for('0') && glyph_for('?') ? "FONT_RENDER_OK" : "FONT_RENDER_FAILED");
    serial::line(desktop_radius >= 4U && desktop_radius <= 24U ? "UI_ROUNDED_RECT_OK" : "UI_ROUNDED_RECT_FAILED");
    serial::line(current_theme.canvas_top != current_theme.canvas_bottom ? "UI_GRADIENT_OK" : "UI_GRADIENT_FAILED");
    const bool bounds_ok = window_x >= 0 && window_y >= 0 && window_x + window_width <= static_cast<int32_t>(width) && window_y + window_height <= static_cast<int32_t>(height);
    serial::line(bounds_ok ? "UI_LAYOUT_BOUNDS_OK" : "UI_LAYOUT_BOUNDS_FAILED");
    const bool geometry_ok = window_x * 2 + window_width == static_cast<int32_t>(width) &&
        topbar_x * 2 + topbar_width == static_cast<int32_t>(width) &&
        dock_x * 2 + dock_width == static_cast<int32_t>(width) &&
        window_width % ui_grid == 0 && window_height % ui_grid == 0;
    serial::line(geometry_ok ? "UI_GEOMETRY_OK" : "UI_GEOMETRY_FAILED");
    const bool navigation_ok = workspace_for_view(View::terminal) == 0U && workspace_for_view(View::files) == 1U &&
        workspace_for_view(View::settings) == 2U && workspace_for_view(View::launcher) == 2U &&
        launcher_card_width >= 40 && launcher_card_height >= 40 && key_f8 > key_f5;
    serial::line(navigation_ok ? "UI_NAVIGATION_MODEL_OK" : "UI_NAVIGATION_MODEL_FAILED");
    serial::write("DESKTOP_SCENE_HASH="); serial_hex(scene_hash()); serial::write("\r\n");
    serial::line("DESKTOP_SCENE_OK");
    return clipping && runtime_preferences_loaded && bounds_ok && geometry_ok && navigation_ok;
}

bool hit(int32_t x, int32_t y, int32_t rect_x, int32_t rect_y, int32_t rect_width, int32_t rect_height) {
    return x >= rect_x && x < rect_x + rect_width && y >= rect_y && y < rect_y + rect_height;
}

View view_for_workspace(uint8_t workspace) {
    if (workspace == 0U) return View::terminal;
    if (workspace == 1U) return View::files;
    return View::launcher;
}

void set_active_view(View view) {
    active_view = view;
    hovered_target = HoverTarget::none;
}

void refresh_desktop() {
    render_scene();
    present_full();
}

HoverTarget hover_target_at(int32_t x, int32_t y) {
    if (hit(x, y, workspace_group_x, workspace_group_y, workspace_item_width, workspace_group_height)) return HoverTarget::workspace_one;
    if (hit(x, y, workspace_group_x + workspace_item_width, workspace_group_y, workspace_item_width, workspace_group_height)) return HoverTarget::workspace_two;
    if (hit(x, y, workspace_group_x + workspace_item_width * 2, workspace_group_y, workspace_item_width, workspace_group_height)) return HoverTarget::workspace_three;
    if (hit(x, y, dock_x + 8, dock_y + 2, 48, 48)) return HoverTarget::dock_terminal;
    if (hit(x, y, dock_x + 64, dock_y + 2, 48, 48)) return HoverTarget::dock_files;
    if (hit(x, y, dock_x + 120, dock_y + 2, 48, 48)) return HoverTarget::dock_settings;
    if (hit(x, y, dock_x + 176, dock_y + 2, 48, 48)) return HoverTarget::dock_launcher;
    if (active_view == View::launcher) {
        if (hit(x, y, window_x + launcher_card_x_offsets[0], window_y + launcher_card_y_offset, launcher_card_width, launcher_card_height)) return HoverTarget::launcher_terminal;
        if (hit(x, y, window_x + launcher_card_x_offsets[1], window_y + launcher_card_y_offset, launcher_card_width, launcher_card_height)) return HoverTarget::launcher_files;
        if (hit(x, y, window_x + launcher_card_x_offsets[2], window_y + launcher_card_y_offset, launcher_card_width, launcher_card_height)) return HoverTarget::launcher_settings;
    }
    if (active_view == View::settings) {
        if (hit(x, y, window_x + 16, window_y + 92, 144, 80)) return HoverTarget::theme_midnight;
        if (hit(x, y, window_x + 176, window_y + 92, 144, 80)) return HoverTarget::theme_graphite;
        if (hit(x, y, window_x + 336, window_y + 92, 144, 80)) return HoverTarget::theme_amber;
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
    else if (active_view == View::launcher && hit(mouse_x, mouse_y, window_x + launcher_card_x_offsets[0], window_y + launcher_card_y_offset, launcher_card_width, launcher_card_height)) { launcher_focus = 0; set_active_view(View::terminal); }
    else if (active_view == View::launcher && hit(mouse_x, mouse_y, window_x + launcher_card_x_offsets[1], window_y + launcher_card_y_offset, launcher_card_width, launcher_card_height)) { launcher_focus = 1; set_active_view(View::files); }
    else if (active_view == View::launcher && hit(mouse_x, mouse_y, window_x + launcher_card_x_offsets[2], window_y + launcher_card_y_offset, launcher_card_width, launcher_card_height)) { launcher_focus = 2; set_active_view(View::settings); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 16, window_y + 92, 144, 80)) { settings_focus = 0; apply_runtime_theme(0); persist_runtime_theme(0); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 176, window_y + 92, 144, 80)) { settings_focus = 1; apply_runtime_theme(1); persist_runtime_theme(1); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 336, window_y + 92, 144, 80)) { settings_focus = 2; apply_runtime_theme(2); persist_runtime_theme(2); }
    else return false;
    refresh_desktop();
    return true;
}

bool handle_navigation_key(uint16_t key) {
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
    if (key == key_escape) {
        set_active_view(View::terminal);
        refresh_desktop();
        return true;
    }
    if (active_view == View::launcher) {
        if (key == key_left || key == key_up) launcher_focus = launcher_focus == 0U ? 2U : static_cast<uint8_t>(launcher_focus - 1U);
        else if (key == key_right || key == key_down || key == '\t') launcher_focus = static_cast<uint8_t>((launcher_focus + 1U) % 3U);
        else if (key == '\n' || key == ' ') {
            set_active_view(launcher_focus == 0U ? View::terminal : (launcher_focus == 1U ? View::files : View::settings));
            refresh_desktop();
            return true;
        } else return true;
        refresh_desktop();
        return true;
    }
    if (active_view == View::settings) {
        if (key == key_left || key == key_up) settings_focus = settings_focus == 0U ? 2U : static_cast<uint8_t>(settings_focus - 1U);
        else if (key == key_right || key == key_down || key == '\t') settings_focus = static_cast<uint8_t>((settings_focus + 1U) % 3U);
        else if (key == '\n' || key == ' ') {
            apply_runtime_theme(settings_focus);
            persist_runtime_theme(settings_focus);
            refresh_desktop();
            return true;
        } else return true;
        refresh_desktop();
        return true;
    }
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    if (!active) return;
    restore_cursor(mouse_x, mouse_y);
    mouse_x += delta_x;
    mouse_y += delta_y;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= static_cast<int32_t>(width)) mouse_x = static_cast<int32_t>(width) - 1;
    if (mouse_y >= static_cast<int32_t>(height)) mouse_y = static_cast<int32_t>(height) - 1;
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
        refresh_desktop();
        scene_changed = true;
        if (!drag_reported) { serial::line("WINDOW_DRAG_OK"); drag_reported = true; }
    } else {
        const HoverTarget next_hover = hover_target_at(mouse_x, mouse_y);
        if (next_hover != hovered_target) {
            hovered_target = next_hover;
            refresh_desktop();
            scene_changed = true;
        }
    }
    if (!scene_changed) draw_cursor();
    mouse_buttons = buttons;
    if (first_mouse_packet) { serial::line("MOUSE_PACKET_OK"); first_mouse_packet = false; }
}
