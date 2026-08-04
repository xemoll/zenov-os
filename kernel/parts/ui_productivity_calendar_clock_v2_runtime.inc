bool productivity_calendar_clock_v2_custom_open() {
    return productivity_overlay_open && !productivity_tasks_open &&
        (productivity_app == ProductivityApp::calendar || productivity_app == ProductivityApp::clock);
}

void render_scene() {
    if (!productivity_calendar_clock_v2_custom_open()) {
        render_scene_tasks_base();
        productivity_clock_v2_render_banner();
        return;
    }
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    const int32_t saved_window_y = window_y;
    window_y += ui_scene_offset_y;
    if (productivity_app == ProductivityApp::calendar) productivity_calendar_v2_render();
    else productivity_clock_v2_render();
    window_y = saved_window_y;
    draw_taskbar();
    draw_quick_settings();
    draw_status_center();
    productivity_clock_v2_render_banner();
}

bool init() {
    const bool initialized = init_tasks_base();
    if (!initialized) return false;
    char normalized[48]{};
    if (!storage::browser_directory_exists("/calendar", normalized)) (void)storage::make_directory("/calendar");
    if (!storage::browser_directory_exists("/clock", normalized)) (void)storage::make_directory("/clock");
    productivity_calendar_today();
    productivity_calendar_v2_load();
    productivity_clock_v2_load_alarms();
    serial::line("UI_CALENDAR_CLOCK_V2_READY calendar=timed+duration+recurrence+edit alarms=persistent timers=three stopwatch=laps");
    return true;
}

bool handle_navigation_key(uint16_t key) {
    if (productivity_tasks_open || !productivity_overlay_open || productivity_app == ProductivityApp::notes)
        return handle_navigation_key_tasks_base(key);
    if (key == key_escape) {
        productivity_calendar_v2_editing = false;
        productivity_clock_v2_alarm_editing = false;
        productivity_close();
        refresh_desktop();
        return true;
    }
    bool handled = false;
    if (productivity_app == ProductivityApp::calendar) handled = productivity_calendar_v2_handle_key(key);
    else if (productivity_app == ProductivityApp::clock) handled = productivity_clock_v2_handle_key(key);
    if (handled) refresh_desktop();
    return true;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    const bool custom_was_open = productivity_calendar_clock_v2_custom_open();
    on_mouse_packet_tasks_base(delta_x, delta_y, buttons);
    if (custom_was_open && productivity_overlay_open) refresh_desktop();
}
