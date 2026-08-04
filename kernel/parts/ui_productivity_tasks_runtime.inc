bool productivity_launcher_query_is(const char* name) {
    if (!name || launcher_query_length != string_length(name)) return false;
    for (uint32_t index = 0U; index < launcher_query_length; ++index)
        if (!productivity_ascii_equal_ci(launcher_query[index], name[index])) return false;
    return true;
}

void productivity_open_launcher_result(ProductivityApp app) {
    launcher_query_length = 0U;
    launcher_query[0] = 0;
    launcher_focus = 0U;
    set_active_view(launcher_return_view);
    productivity_open(app);
    refresh_desktop();
}

void productivity_open_tasks_launcher_result() {
    launcher_query_length = 0U;
    launcher_query[0] = 0;
    launcher_focus = 0U;
    set_active_view(launcher_return_view);
    productivity_tasks_open_app();
    refresh_desktop();
}

void render_scene() {
    if (!productivity_tasks_open) {
        render_scene_productivity_base();
        return;
    }
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    const int32_t saved_window_y = window_y;
    window_y += ui_scene_offset_y;
    productivity_tasks_render();
    window_y = saved_window_y;
    draw_taskbar();
    draw_quick_settings();
    draw_status_center();
}

bool init() {
    const bool initialized = init_productivity_base();
    if (initialized) serial::line("UI_PRODUCTIVITY_TASKS_READY markdown=yes metadata=priority+due+waiting views=today+overdue+planned crud=add+edit+toggle+delete");
    return initialized;
}

bool handle_navigation_key(uint16_t key) {
    if (productivity_tasks_open) {
        const bool handled = productivity_tasks_handle_key(key);
        if (handled) refresh_desktop();
        return true;
    }
    if (!productivity_overlay_open) {
        if (active_view == View::launcher && (key == '\n' || key == ' ')) {
            if (productivity_launcher_query_is("notes") || productivity_launcher_query_is("notepad")) {
                productivity_open_launcher_result(ProductivityApp::notes);
                return true;
            }
            if (productivity_launcher_query_is("calendar")) {
                productivity_open_launcher_result(ProductivityApp::calendar);
                return true;
            }
            if (productivity_launcher_query_is("clock") || productivity_launcher_query_is("time")) {
                productivity_open_launcher_result(ProductivityApp::clock);
                return true;
            }
            if (productivity_launcher_query_is("tasks") || productivity_launcher_query_is("planner") || productivity_launcher_query_is("todo")) {
                productivity_open_tasks_launcher_result();
                return true;
            }
        }
        if (key == key_f1 || key == key_f2 || key == key_f3)
            return handle_navigation_key_files_base(key);
    }
    return handle_navigation_key_productivity_base(key);
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    const bool tasks_were_open = productivity_tasks_open;
    on_mouse_packet_productivity_base(delta_x, delta_y, buttons);
    if (tasks_were_open && !productivity_overlay_open) productivity_tasks_open = false;
}
