bool productivity_notes_v2_handle_editor_key(uint16_t key) {
    productivity_notes_v2_bind();
    if (key == key_f2) {
        (void)productivity_notes_v2_save();
        return true;
    }
    if (key == key_f5) {
        (void)productivity_notes_v2_undo_once();
        return true;
    }
    if (key == key_f6) {
        (void)productivity_notes_v2_redo_once();
        return true;
    }
    if (key == key_escape) {
        productivity_notes_v2_close_to_vault();
        return true;
    }
    if (key == key_left) {
        productivity_notes_v2_move_horizontal(false);
        return true;
    }
    if (key == key_right) {
        productivity_notes_v2_move_horizontal(true);
        return true;
    }
    if (key == key_up) {
        productivity_notes_v2_move_vertical(false);
        return true;
    }
    if (key == key_down) {
        productivity_notes_v2_move_vertical(true);
        return true;
    }
    if (key == key_home) {
        productivity_notes_v2_move_home();
        return true;
    }
    if (key == key_end) {
        productivity_notes_v2_move_end();
        return true;
    }
    if (key == key_delete) {
        (void)productivity_notes_v2_delete_forward();
        return true;
    }
    if (key == '\b') {
        (void)productivity_notes_v2_backspace();
        return true;
    }
    if (key == '\t') {
        (void)productivity_notes_v2_insert_tab();
        return true;
    }
    if (key == '\n') {
        (void)productivity_notes_v2_insert_char('\n');
        return true;
    }
    if (key >= 32U && key <= 126U) {
        (void)productivity_notes_v2_insert_char(static_cast<char>(key));
        return true;
    }
    return true;
}

void render_scene() {
    if (!productivity_notes_v2_is_active()) {
        if (productivity_notes_v2_bound) productivity_notes_v2_unbind();
        render_scene_utilities_base();
        return;
    }
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    const int32_t saved_window_y = window_y;
    window_y += ui_scene_offset_y;
    productivity_notes_v2_render_editor();
    window_y = saved_window_y;
    draw_taskbar();
    draw_quick_settings();
    draw_status_center();
    productivity_clock_v2_render_banner();
    productivity_reminder_render_banner();
}

void render_scene_top() {
    render_scene();
}

bool init() {
    const bool initialized = init_utilities_base();
    if (!initialized) return false;
    productivity_notes_v2_unbind();
    serial::line("UI_NOTES_V2_READY cursor=insert+delete navigation=arrows+home+end history=undo8+redo8 viewport=logical-lines");
    return true;
}

bool handle_navigation_key(uint16_t key) {
    if (!productivity_notes_v2_is_active()) {
        const bool handled = handle_navigation_key_utilities_base(key);
        if (productivity_notes_v2_is_active()) productivity_notes_v2_bind();
        else if (productivity_notes_v2_bound) productivity_notes_v2_unbind();
        return handled;
    }

    if (key == key_f7 || key == key_f8 || key == key_f9 || key == key_f10 || key == key_f11 || key == key_super) {
        if (productivity_note_dirty) (void)productivity_notes_v2_save();
        productivity_note_editing = false;
        productivity_note_path[0] = 0;
        productivity_notes_v2_unbind();
        return handle_navigation_key_utilities_base(key);
    }

    const bool handled = productivity_notes_v2_handle_editor_key(key);
    if (handled) refresh_desktop();
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    const bool was_active = productivity_notes_v2_is_active();
    on_mouse_packet_utilities_base(delta_x, delta_y, buttons);
    if (was_active && !productivity_notes_v2_is_active()) productivity_notes_v2_unbind();
    else if (productivity_notes_v2_is_active()) productivity_notes_v2_bind();
}
