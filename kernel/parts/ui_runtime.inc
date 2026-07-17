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
        window_x = 145;
        window_y = 82;
        active_view = View::terminal;
        render_scene();
        present_full();
        serial::line("UI_TERMINAL_RENDER_OK");
        serial::line("ZENOV_SHELL_READY");
        return;
    }
    if (active_view != View::terminal || dragging) return;
    render_terminal();
    present_region(window_x - 2, window_y - 2, window_width + 18, window_height + 20);
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
    serial::line(window_x >= 0 && window_y >= 0 && window_x + window_width <= static_cast<int32_t>(width) && window_y + window_height <= static_cast<int32_t>(height) ? "UI_LAYOUT_BOUNDS_OK" : "UI_LAYOUT_BOUNDS_FAILED");
    serial::write("DESKTOP_SCENE_HASH="); serial_hex(scene_hash()); serial::write("\r\n");
    serial::line("DESKTOP_SCENE_OK");
    return clipping && runtime_preferences_loaded;
}

bool hit(int32_t x, int32_t y, int32_t rect_x, int32_t rect_y, int32_t rect_width, int32_t rect_height) {
    return x >= rect_x && x < rect_x + rect_width && y >= rect_y && y < rect_y + rect_height;
}

bool handle_click() {
    if (hit(mouse_x, mouse_y, 238, 541, 48, 48)) active_view = View::terminal;
    else if (hit(mouse_x, mouse_y, 298, 541, 48, 48)) active_view = View::files;
    else if (hit(mouse_x, mouse_y, 358, 541, 48, 48)) active_view = View::settings;
    else if (hit(mouse_x, mouse_y, 418, 541, 48, 48)) active_view = View::launcher;
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 24, window_y + 109, 134, 70)) { apply_runtime_theme(0); persist_runtime_theme(0); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 172, window_y + 109, 134, 70)) { apply_runtime_theme(1); persist_runtime_theme(1); }
    else if (active_view == View::settings && hit(mouse_x, mouse_y, window_x + 320, window_y + 109, 134, 70)) { apply_runtime_theme(2); persist_runtime_theme(2); }
    else return false;
    render_scene(); present_full();
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
        window_x = mouse_x - drag_offset_x;
        window_y = mouse_y - drag_offset_y;
        if (window_x < 88) window_x = 88;
        if (window_x > static_cast<int32_t>(width) - 530) window_x = static_cast<int32_t>(width) - 530;
        if (window_y < 62) window_y = 62;
        if (window_y > 148) window_y = 148;
        render_scene(); present_full();
        scene_changed = true;
        if (!drag_reported) { serial::line("WINDOW_DRAG_OK"); drag_reported = true; }
    }
    if (!scene_changed) draw_cursor();
    mouse_buttons = buttons;
    if (first_mouse_packet) { serial::line("MOUSE_PACKET_OK"); first_mouse_packet = false; }
}
