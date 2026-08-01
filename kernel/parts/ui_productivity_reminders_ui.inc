const char* productivity_reminder_filter_text() {
    if (productivity_reminder_filter == ProductivityReminderFilter::overdue) return "OVERDUE";
    if (productivity_reminder_filter == ProductivityReminderFilter::scheduled) return "SCHEDULED";
    if (productivity_reminder_filter == ProductivityReminderFilter::done) return "DONE";
    return "TODAY";
}

const char* productivity_agenda_kind_text(ProductivityAgendaKind kind) {
    if (kind == ProductivityAgendaKind::task) return "TASK";
    if (kind == ProductivityAgendaKind::event) return "EVENT";
    return "REMINDER";
}

void productivity_reminders_render() {
    draw_window_chrome("ZEN REMINDERS", "LOCAL SMART AGENDA");
    const int32_t x = window_x + 16, y = window_y + 62;
    fill_round_rect(x, y, 480, 38, 8, current_theme.surface_low);
    draw_text(x + 14, y + 15, productivity_reminder_filter_text(), current_theme.accent, 1);
    draw_text_right(x + 462, y + 15, "F1 TODAY  F2 LATE  F3 PLAN  F4 ADD  F5 DONE", current_theme.text_muted, 1);

    fill_round_rect(x, y + 48, 342, 246, 8, current_theme.surface_low);
    for (uint8_t row = 0U; row < productivity_agenda_visible_rows; ++row) {
        const uint8_t item_index = static_cast<uint8_t>(productivity_agenda_scroll + row);
        if (item_index >= productivity_agenda_count) break;
        const ProductivityAgendaItem& item = productivity_agenda[item_index];
        const int32_t row_y = y + 66 + static_cast<int32_t>(row * 27U);
        if (item_index == productivity_agenda_focus) fill_round_rect(x + 8, row_y - 9, 326, 23, 6, current_theme.accent_soft);
        draw_text(x + 16, row_y, item.done ? "[x]" : (item.kind == ProductivityAgendaKind::event ? "[E]" : "[ ]"),
            item.done ? current_theme.success : (item.overdue ? current_theme.warning : current_theme.text), 1);
        char title[31]{}; string_copy(title, item.title, sizeof(title));
        draw_text(x + 50, row_y, title, item.done ? current_theme.text_muted : current_theme.text, 1);
        char time[6]{}; productivity_time_text(time, item.hour, item.minute);
        draw_text_right(x + 326, row_y, time, item.overdue ? current_theme.warning : current_theme.text_muted, 1);
    }
    if (!productivity_agenda_count) draw_text(x + 18, y + 112, "NO ITEMS IN THIS SMART VIEW", current_theme.text_muted, 1);

    fill_round_rect(x + 352, y + 48, 128, 246, 8, current_theme.surface_high);
    draw_text(x + 364, y + 64, "DETAILS", current_theme.text_muted, 1);
    if (productivity_agenda_count) {
        const ProductivityAgendaItem& item = productivity_agenda[productivity_agenda_focus];
        draw_text(x + 364, y + 92, productivity_agenda_kind_text(item.kind), current_theme.accent, 1);
        char date[11]{}; productivity_date_text(date, item.year, item.month, item.day);
        char time[6]{}; productivity_time_text(time, item.hour, item.minute);
        draw_text(x + 364, y + 124, "DATE", current_theme.text_muted, 1);
        draw_text(x + 364, y + 144, date, item.overdue ? current_theme.warning : current_theme.text, 1);
        draw_text(x + 364, y + 174, "TIME", current_theme.text_muted, 1);
        draw_text(x + 364, y + 194, time, current_theme.text, 1);
        draw_text(x + 364, y + 224, item.done ? "COMPLETED" : (item.overdue ? "OVERDUE" : "ACTIVE"),
            item.done ? current_theme.success : (item.overdue ? current_theme.warning : current_theme.text), 1);
    }
    if (productivity_reminder_adding) {
        fill_round_rect(x, y + 306, 480, 34, 8, current_theme.accent_soft);
        draw_text(x + 12, y + 319, productivity_reminder_input_size ? productivity_reminder_input : "TYPE REMINDER TITLE (DUE IN 1 MIN)",
            productivity_reminder_input_size ? current_theme.text : current_theme.text_muted, 1);
    } else draw_text(x, y + 316, "ENTER TOGGLE  S SNOOZE  O OPEN  DEL REMOVE  ESC CLOSE", current_theme.text_muted, 1);
}

void productivity_reminder_render_banner() {
    if (!productivity_reminder_banner_active) return;
    const int32_t x = static_cast<int32_t>(width) - 354;
    const int32_t y = 48;
    fill_round_rect(x, y, 330, 58, 10, current_theme.surface_high);
    draw_text(x + 14, y + 17, "REMINDER DUE", current_theme.warning, 1);
    char title[36]{}; string_copy(title, productivity_reminder_banner_title, sizeof(title));
    draw_text(x + 14, y + 39, title, current_theme.text, 1);
}

void productivity_utility_open(ProductivityUtilityApp app) {
    if (productivity_note_dirty) (void)productivity_notes_save();
    productivity_tasks_open = false;
    productivity_utility_app = app;
    productivity_overlay_open = true;
    productivity_note_searching = false;
    productivity_event_editing = false;
    productivity_reminder_adding = false;
    if (app == ProductivityUtilityApp::calculator) {
        productivity_calculator_load_state();
        productivity_calculator_select_mode(ProductivityCalculatorMode::standard);
        serial::line("UI_CALCULATOR_OPEN_OK");
    } else if (app == ProductivityUtilityApp::reminders) {
        productivity_reminders_load();
        productivity_agenda_rebuild();
        serial::line("UI_REMINDERS_OPEN_OK");
    }
}

void productivity_utility_close() {
    productivity_utility_app = ProductivityUtilityApp::none;
    productivity_reminder_adding = false;
    productivity_reminder_input_size = 0U;
    productivity_reminder_input[0] = 0;
    productivity_close();
}

