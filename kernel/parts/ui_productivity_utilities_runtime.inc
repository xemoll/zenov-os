uint32_t productivity_background_last_tick = 0U;

void background_tick() {
    if (!active || !backbuffer) return;
    const uint32_t current_tick = timer_ticks;
    if (current_tick - productivity_background_last_tick < 100U) return;
    productivity_background_last_tick = current_tick;
    if (!productivity_reminders_check_due()) return;
    render_scene();
    present_full();
    serial::line("UI_BACKGROUND_REMINDER_REFRESH_OK");
}

void productivity_open_utility_launcher_result(ProductivityUtilityApp app) {
    launcher_query_length = 0U;
    launcher_query[0] = 0;
    launcher_focus = 0U;
    set_active_view(launcher_return_view);
    productivity_utility_open(app);
    refresh_desktop();
}

void render_scene() {
    (void)productivity_reminders_check_due();
    if (productivity_utility_app == ProductivityUtilityApp::none) {
        render_scene_tasks_base();
        productivity_reminder_render_banner();
        return;
    }
    if (!backbuffer) return;
    draw_wallpaper();
    draw_topbar();
    const int32_t saved_window_y = window_y;
    window_y += ui_scene_offset_y;
    if (productivity_utility_app == ProductivityUtilityApp::calculator) productivity_calculator_render();
    else productivity_reminders_render();
    window_y = saved_window_y;
    draw_taskbar();
    draw_quick_settings();
    draw_status_center();
    productivity_reminder_render_banner();
}

bool init() {
    const bool initialized = init_tasks_base();
    if (!initialized) return false;
    (void)productivity_utilities_ensure_storage();
    productivity_calculator_load_state();
    productivity_reminders_load();
    productivity_agenda_rebuild();
    productivity_background_last_tick = timer_ticks;
    serial::line("UI_PRODUCTIVITY_UTILITIES_READY calculator=standard+programmer+date+units reminders=smart+agenda+recurrence+quick-capture+background");
    return true;
}

bool handle_navigation_key(uint16_t key) {
    if (productivity_utility_app == ProductivityUtilityApp::calculator) {
        const bool handled = productivity_calculator_handle_key(key);
        if (handled) refresh_desktop();
        return true;
    }
    if (productivity_utility_app == ProductivityUtilityApp::reminders) {
        const bool handled = productivity_reminders_handle_key(key);
        if (handled) refresh_desktop();
        return true;
    }
    if (!productivity_overlay_open && active_view == View::launcher && (key == '\n' || key == ' ')) {
        if (productivity_launcher_query_is("calculator") || productivity_launcher_query_is("calc")) {
            productivity_open_utility_launcher_result(ProductivityUtilityApp::calculator);
            return true;
        }
        if (productivity_launcher_query_is("reminders") || productivity_launcher_query_is("reminder") ||
            productivity_launcher_query_is("agenda") || productivity_launcher_query_is("alarm")) {
            productivity_open_utility_launcher_result(ProductivityUtilityApp::reminders);
            return true;
        }
    }
    return handle_navigation_key_tasks_base(key);
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    const ProductivityUtilityApp previous = productivity_utility_app;
    on_mouse_packet_tasks_base(delta_x, delta_y, buttons);
    if (previous != ProductivityUtilityApp::none && !productivity_overlay_open)
        productivity_utility_app = ProductivityUtilityApp::none;
}
