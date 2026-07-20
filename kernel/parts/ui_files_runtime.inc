int32_t files_last_click_index = -1;
uint32_t files_last_click_tick = 0;

void files_clear_hover() {
    files_hover_row = -1;
    files_hover_place = 0xFFU;
}

void files_update_hover(int32_t x, int32_t y) {
    files_clear_hover();
    if (active_view != View::files || files_preview_open) return;
    for (uint8_t i = 0; i < 5U; ++i) {
        if (hit(x, y, window_x + files_sidebar_x_offset, window_y + 88 + static_cast<int32_t>(i * 38U), files_sidebar_width, 30)) {
            files_hover_place = i;
            return;
        }
    }
    const int32_t content_x = window_x + files_content_x_offset;
    if (!hit(x, y, content_x, window_y + files_row_y_offset, files_content_width,
             static_cast<int32_t>(files_visible_rows * files_row_height))) return;
    const uint32_t visible = static_cast<uint32_t>((y - (window_y + files_row_y_offset)) / files_row_height);
    const uint32_t index = files_scroll + visible;
    if (index < files_entry_count) files_hover_row = static_cast<int32_t>(index);
}

bool files_handle_click_at(int32_t x, int32_t y) {
    if (active_view != View::files) return false;
    if (files_preview_open) {
        if (hit(x, y, window_x + 16, window_y + 348, 480, 40)) {
            files_close_preview();
            return true;
        }
        return false;
    }

    const int32_t content_x = window_x + files_content_x_offset;
    if (hit(x, y, content_x + 6, window_y + 66, 24, 24)) {
        (void)files_go_parent();
        return true;
    }
    for (uint8_t i = 0; i < 5U; ++i) {
        if (hit(x, y, window_x + files_sidebar_x_offset, window_y + 88 + static_cast<int32_t>(i * 38U), files_sidebar_width, 30)) {
            (void)files_set_path(files_place_paths[i]);
            return true;
        }
    }
    if (!hit(x, y, content_x, window_y + files_row_y_offset, files_content_width,
             static_cast<int32_t>(files_visible_rows * files_row_height))) return false;
    const uint32_t visible = static_cast<uint32_t>((y - (window_y + files_row_y_offset)) / files_row_height);
    const uint32_t index = files_scroll + visible;
    if (index >= files_entry_count) return false;

    const bool double_click = files_last_click_index == static_cast<int32_t>(index) &&
        static_cast<uint32_t>(timer_ticks - files_last_click_tick) <= 50U;
    files_focus = index;
    files_ensure_visible();
    files_last_click_index = static_cast<int32_t>(index);
    files_last_click_tick = timer_ticks;
    if (double_click) (void)files_open_focused();
    return true;
}

bool init() {
    const bool initialized = init_base();
    if (!initialized) return false;
    serial::line("GRAPHICS_MODE_OK 800x600x32 LOGICAL_CANVAS");
    files_reload();
    storage::BrowserEntry app_probe[4]{};
    const bool browser_ok = storage::browser_mounted() && files_entry_count > 0U && storage::list_directory("/apps", app_probe, 4U) > 0U;
    serial::line(browser_ok ? "UI_FILES_BROWSER_OK" : "UI_FILES_BROWSER_FAILED");
    return initialized;
}

bool handle_navigation_key(uint16_t key) {
    if (active_view != View::files) return handle_navigation_key_base(key);
    if (key == key_f5 || key == key_f6 || key == key_f7 || key == key_f8 || key == key_f9) return handle_navigation_key_base(key);

    if (files_preview_open) {
        if (key == '\b' || key == key_escape) {
            files_close_preview();
            refresh_desktop();
        }
        return true;
    }
    if (key == key_escape) return handle_navigation_key_base(key);
    if (key == '\b' || key == key_left) {
        if (files_go_parent()) refresh_desktop();
        return true;
    }
    files_reload();
    if (files_entry_count == 0U) return true;
    if (key == key_up) files_focus = files_focus == 0U ? files_entry_count - 1U : files_focus - 1U;
    else if (key == key_down || key == key_right || key == '\t') files_focus = (files_focus + 1U) % files_entry_count;
    else if (key == key_home) files_focus = 0U;
    else if (key == key_end) files_focus = files_entry_count - 1U;
    else if (key == '\n' || key == ' ') (void)files_open_focused();
    else return true;
    files_ensure_visible();
    refresh_desktop();
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    if (active_view != View::files) {
        on_mouse_packet_base(delta_x, delta_y, buttons);
        return;
    }
    if (!active) return;
    restore_cursor(mouse_screen_x, mouse_screen_y);
    mouse_screen_x += delta_x;
    mouse_screen_y += delta_y;
    if (mouse_screen_x < 0) mouse_screen_x = 0;
    if (mouse_screen_y < 0) mouse_screen_y = 0;
    if (mouse_screen_x >= static_cast<int32_t>(display_width)) mouse_screen_x = static_cast<int32_t>(display_width) - 1;
    if (mouse_screen_y >= static_cast<int32_t>(display_height)) mouse_screen_y = static_cast<int32_t>(display_height) - 1;
    update_logical_mouse();

    const bool left = (buttons & 1U) != 0;
    const bool previous_left = (mouse_buttons & 1U) != 0;
    bool scene_changed = false;
    if (left && !previous_left) {
        const View previous_view = active_view;
        if (handle_click()) {
            if (previous_view != active_view && active_view == View::files) files_reload();
            scene_changed = true;
        } else if (files_handle_click_at(mouse_x, mouse_y)) {
            refresh_desktop();
            scene_changed = true;
        } else if (mouse_x >= window_x && mouse_x < window_x + window_width && mouse_y >= window_y && mouse_y < window_y + titlebar_height) {
            dragging = true;
            drag_offset_x = mouse_x - window_x;
            drag_offset_y = mouse_y - window_y;
        }
    }
    if (!left) dragging = false;
    if (dragging) {
        hovered_target = HoverTarget::none;
        files_clear_hover();
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
        const HoverTarget previous_hover = hovered_target;
        const int32_t previous_row = files_hover_row;
        const uint8_t previous_place = files_hover_place;
        hovered_target = hover_target_at(mouse_x, mouse_y);
        files_update_hover(mouse_x, mouse_y);
        if (previous_hover != hovered_target || previous_row != files_hover_row || previous_place != files_hover_place) {
            refresh_desktop();
            scene_changed = true;
        }
    }
    if (!scene_changed) draw_cursor();
    mouse_buttons = buttons;
    if (first_mouse_packet) { serial::line("MOUSE_PACKET_OK"); first_mouse_packet = false; }
}
