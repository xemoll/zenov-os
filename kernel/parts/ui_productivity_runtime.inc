bool init() {
    const bool initialized = init_files();
    if (!initialized) return false;
    const bool model_ok = productivity::model_contract_ok();
    serial::line(model_ok ? "UI_PRODUCTIVITY_MODEL_OK" : "UI_PRODUCTIVITY_MODEL_FAILED");
    serial::line("UI_SYSTEM_APPS_READY clock=yes calendar=yes notes=zenovfs notepad=zenovfs");
    return initialized && model_ok;
}

bool handle_navigation_key(uint16_t key) {
    if (!productivity::opened) {
        if (active_view == View::launcher && !quick_settings_open && !status_center_open &&
            (key == '\n' || key == ' ') && launcher_focus == 3U) {
            productivity::open_from_launcher();
            return true;
        }
        const bool handled = handle_navigation_key_files(key);
        if (active_view == View::launcher && productivity::workspace_query_matches()) {
            launcher_focus = 3U;
            refresh_desktop();
        }
        return handled;
    }

    keyboard_focus_mode = true;
    hovered_target = HoverTarget::none;
    if (key == key_escape) { productivity::close(); return true; }
    if (key == key_f5 || key == key_f6 || key == key_f7 || key == key_f8 || key == key_f9 || key == key_f10 || key == key_f11 || key == key_super) {
        productivity::opened = false;
        active_view = productivity::return_view;
        return handle_navigation_key_files(key);
    }
    if (key == '\t' || key == key_f3) {
        const uint8_t next = static_cast<uint8_t>((static_cast<uint8_t>(productivity::page) + 1U) % productivity::page_count);
        productivity::select_page(static_cast<productivity::Page>(next)); refresh_desktop(); return true;
    }

    if (productivity::page == productivity::Page::calendar) {
        if (key == key_left) (void)productivity_model::shift_days(productivity::selected_date, -1);
        else if (key == key_right) (void)productivity_model::shift_days(productivity::selected_date, 1);
        else if (key == key_up) (void)productivity_model::shift_days(productivity::selected_date, -7);
        else if (key == key_down) (void)productivity_model::shift_days(productivity::selected_date, 7);
        else if (key == key_home) { productivity::ensure_today(); productivity::selected_date = productivity::today; }
        else if (key == key_end) (void)productivity_model::shift_month(productivity::selected_date, 1);
        else if (key == '\n' || key == ' ') { productivity::daily_state = productivity::DocumentState::unloaded; productivity::select_page(productivity::Page::notes); }
        else return true;
        refresh_desktop(); return true;
    }

    if (productivity::page == productivity::Page::notes || productivity::page == productivity::Page::notepad) {
        productivity_model::Editor& editor = productivity::page == productivity::Page::notes ? productivity::daily_editor : productivity::scratch_editor;
        productivity::DocumentState& state = productivity::page == productivity::Page::notes ? productivity::daily_state : productivity::scratch_state;
        if (key == key_f2) {
            char path[48];
            if (productivity::page == productivity::Page::notes) productivity::build_daily_path(productivity::selected_date, path);
            else string_copy(path, "/docs/scratchpad.txt", sizeof(path));
            (void)productivity::save_editor(editor, path, state); refresh_desktop(); return true;
        }
        if (key == key_left) productivity_model::editor_move_horizontal(editor, -1);
        else if (key == key_right) productivity_model::editor_move_horizontal(editor, 1);
        else if (key == key_up) productivity_model::editor_move_line(editor, -1);
        else if (key == key_down) productivity_model::editor_move_line(editor, 1);
        else if (key == key_home) productivity_model::editor_home(editor);
        else if (key == key_end) productivity_model::editor_end(editor);
        else if (key == key_delete) (void)productivity_model::editor_delete(editor);
        else if (key == '\b') (void)productivity_model::editor_backspace(editor);
        else if (key == '\n') (void)productivity_model::editor_insert(editor, '\n');
        else if (key >= 32U && key <= 126U) (void)productivity_model::editor_insert(editor, static_cast<char>(key));
        else return true;
        productivity::editor_ensure_cursor_visible(editor); refresh_desktop(); return true;
    }
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    const int32_t projected_screen_x = mouse_screen_x + delta_x;
    const int32_t projected_screen_y = mouse_screen_y + delta_y;
    int32_t clamped_x = projected_screen_x < 0 ? 0 : projected_screen_x;
    int32_t clamped_y = projected_screen_y < 0 ? 0 : projected_screen_y;
    if (clamped_x >= static_cast<int32_t>(display_width)) clamped_x = static_cast<int32_t>(display_width) - 1;
    if (clamped_y >= static_cast<int32_t>(display_height)) clamped_y = static_cast<int32_t>(display_height) - 1;
    int32_t viewport_screen_x = clamped_x;
    int32_t viewport_screen_y = clamped_y;
    if (viewport_screen_x < viewport_x) viewport_screen_x = viewport_x;
    if (viewport_screen_y < viewport_y) viewport_screen_y = viewport_y;
    if (viewport_screen_x >= viewport_x + viewport_width) viewport_screen_x = viewport_x + viewport_width - 1;
    if (viewport_screen_y >= viewport_y + viewport_height) viewport_screen_y = viewport_y + viewport_height - 1;
    const uint32_t relative_x = static_cast<uint32_t>(viewport_screen_x - viewport_x);
    const uint32_t relative_y = static_cast<uint32_t>(viewport_screen_y - viewport_y);
    int32_t logical_x = static_cast<int32_t>(relative_x * width / static_cast<uint32_t>(viewport_width));
    int32_t logical_y = static_cast<int32_t>(relative_y * height / static_cast<uint32_t>(viewport_height));
    if (logical_x >= static_cast<int32_t>(width)) logical_x = static_cast<int32_t>(width) - 1;
    if (logical_y >= static_cast<int32_t>(height)) logical_y = static_cast<int32_t>(height) - 1;
    const bool left = (buttons & 1U) != 0U, previous_left = (mouse_buttons & 1U) != 0U;

    if (!productivity::opened && active_view == View::launcher && left && !previous_left &&
        hit(logical_x, logical_y, launcher_card_x(3U), launcher_card_y(3U), launcher_card_width, launcher_card_height)) {
        mouse_screen_x = clamped_x; mouse_screen_y = clamped_y; update_logical_mouse(); mouse_buttons = buttons;
        productivity::open_from_launcher(); return;
    }
    if (!productivity::opened) { on_mouse_packet_files(delta_x, delta_y, buttons); return; }
    if (!active) return;
    restore_cursor(mouse_screen_x, mouse_screen_y);
    mouse_screen_x = clamped_x; mouse_screen_y = clamped_y; update_logical_mouse();
    bool changed = delta_x != 0 || delta_y != 0;
    if (left && !previous_left) {
        for (uint8_t index = 0U; index < productivity::page_count; ++index) {
            if (!hit(mouse_x, mouse_y, window_x + productivity::sidebar_x_offset,
                     window_y + 68 + static_cast<int32_t>(index) * 50,
                     productivity::sidebar_width, productivity::page_button_height)) continue;
            productivity::select_page(static_cast<productivity::Page>(index)); changed = true; break;
        }
        if ((productivity::page == productivity::Page::notes || productivity::page == productivity::Page::notepad) &&
            hit(mouse_x, mouse_y, window_x + productivity::content_x_offset, window_y + 348, 90, 28)) {
            productivity_model::Editor& editor = productivity::page == productivity::Page::notes ? productivity::daily_editor : productivity::scratch_editor;
            productivity::DocumentState& state = productivity::page == productivity::Page::notes ? productivity::daily_state : productivity::scratch_state;
            char path[48];
            if (productivity::page == productivity::Page::notes) productivity::build_daily_path(productivity::selected_date, path);
            else string_copy(path, "/docs/scratchpad.txt", sizeof(path));
            (void)productivity::save_editor(editor, path, state); changed = true;
        }
    }
    if (changed) refresh_desktop(); else draw_cursor();
    mouse_buttons = buttons;
}
