const char* productivity_reminder_filter_text() {
    if (productivity_reminder_filter == ProductivityReminderFilter::overdue) return "OVERDUE";
    if (productivity_reminder_filter == ProductivityReminderFilter::week) return "NEXT 7 DAYS";
    if (productivity_reminder_filter == ProductivityReminderFilter::scheduled) return "SCHEDULED";
    if (productivity_reminder_filter == ProductivityReminderFilter::done) return "DONE";
    return "TODAY";
}

const char* productivity_agenda_kind_text(ProductivityAgendaKind kind) {
    if (kind == ProductivityAgendaKind::task) return "TASK";
    if (kind == ProductivityAgendaKind::event) return "EVENT";
    return "REMINDER";
}

void productivity_reminders_draw_filter(int32_t x, int32_t y, const char* key, const char* label, ProductivityReminderFilter filter) {
    const bool selected = productivity_reminder_filter == filter;
    if (selected) {
        fill_round_rect(x, y, 84, 30, 6, current_theme.accent_soft);
        fill_round_rect(x, y + 5, 3, 20, 2, current_theme.accent);
    }
    draw_text(x + 10, y + 12, key, selected ? current_theme.accent : current_theme.text_muted, 1);
    draw_text(x + 30, y + 12, label, selected ? current_theme.text : current_theme.text_muted, 1);
}

ProductivityReminderRepeat productivity_agenda_item_repeat(const ProductivityAgendaItem& item) {
    if (item.kind != ProductivityAgendaKind::reminder || item.source_index >= productivity_reminder_count)
        return ProductivityReminderRepeat::none;
    return productivity_reminders[item.source_index].repeat;
}

void productivity_reminders_render_quick_add(int32_t x, int32_t y) {
    fill_round_rect(x, y + 48, 480, 246, 8, current_theme.surface_low);
    draw_text(x + 16, y + 66, "QUICK CAPTURE", current_theme.accent, 1);
    draw_text_right(x + 464, y + 66, "LOCAL / PERSISTENT", current_theme.text_muted, 1);

    fill_round_rect(x + 16, y + 84, 448, 42, 7, current_theme.accent_soft);
    char visible[53]{};
    if (productivity_reminder_input_size <= 52U) string_copy(visible, productivity_reminder_input, sizeof(visible));
    else {
        visible[0] = '.'; visible[1] = '.'; visible[2] = '.';
        const uint8_t start = static_cast<uint8_t>(productivity_reminder_input_size - 49U);
        for (uint8_t index = 0U; index < 49U; ++index) visible[index + 3U] = productivity_reminder_input[start + index];
    }
    draw_text(x + 28, y + 101,
        productivity_reminder_input_size ? visible : "TITLE @+10m !daily", productivity_reminder_input_size ? current_theme.text : current_theme.text_muted, 1);

    fill_round_rect(x + 16, y + 138, 246, 138, 7, current_theme.surface_high);
    draw_text(x + 28, y + 156, "SCHEDULING", current_theme.text_muted, 1);
    draw_text(x + 28, y + 181, "@+10m   RELATIVE MINUTES", current_theme.text, 1);
    draw_text(x + 28, y + 202, "@+2h    RELATIVE HOURS", current_theme.text, 1);
    draw_text(x + 28, y + 223, "@2026-08-03 09:30", current_theme.text, 1);
    draw_text(x + 28, y + 250, "!daily  !weekly  !monthly", current_theme.text_muted, 1);

    fill_round_rect(x + 272, y + 138, 192, 138, 7, current_theme.surface_high);
    draw_text(x + 284, y + 156, "PREVIEW", current_theme.text_muted, 1);
    ProductivityReminder preview{};
    const bool valid = productivity_reminder_parse_quick(productivity_reminder_input, productivity_reminder_input_size,
        productivity_reminder_add_repeat, preview);
    if (valid) {
        char date[11]{}; productivity_date_text(date, preview.year, preview.month, preview.day);
        char time[6]{}; productivity_time_text(time, preview.hour, preview.minute);
        draw_text(x + 284, y + 181, date, current_theme.text, 1);
        draw_text(x + 284, y + 204, time, current_theme.text, 1);
        draw_text(x + 284, y + 227, productivity_reminder_repeat_text(preview.repeat), current_theme.accent, 1);
        draw_text(x + 284, y + 250, "READY TO SAVE", current_theme.success, 1);
    } else {
        draw_text(x + 284, y + 187, productivity_reminder_repeat_text(productivity_reminder_add_repeat), current_theme.accent, 1);
        draw_text(x + 284, y + 218, productivity_reminder_input_size ? "CHECK SYNTAX" : "START TYPING", current_theme.text_muted, 1);
    }
    draw_text(x, y + 316, "TAB REPEAT   ENTER SAVE   ESC CANCEL", current_theme.text_muted, 1);
}

void productivity_reminders_render() {
    draw_window_chrome("ZEN REMINDERS", "LOCAL AGENDA / RECURRENCE");
    const int32_t x = window_x + 16, y = window_y + 62;
    fill_round_rect(x, y, 480, 38, 8, current_theme.surface_low);
    draw_text(x + 14, y + 15, productivity_reminder_adding ? "QUICK CAPTURE" : productivity_reminder_filter_text(), current_theme.accent, 1);
    draw_text_right(x + 462, y + 15, productivity_reminder_adding ? "TAB CYCLES DEFAULT REPEAT" : "A NEW   R REPEAT   S SNOOZE", current_theme.text_muted, 1);

    if (productivity_reminder_adding) {
        productivity_reminders_render_quick_add(x, y);
        return;
    }

    const int32_t body_y = y + 48;
    fill_round_rect(x, body_y, 100, 246, 8, current_theme.surface_low);
    draw_text(x + 12, body_y + 16, "VIEWS", current_theme.text_muted, 1);
    productivity_reminders_draw_filter(x + 8, body_y + 30, "F1", "TODAY", ProductivityReminderFilter::today);
    productivity_reminders_draw_filter(x + 8, body_y + 66, "F2", "LATE", ProductivityReminderFilter::overdue);
    productivity_reminders_draw_filter(x + 8, body_y + 102, "F3", "7 DAYS", ProductivityReminderFilter::week);
    productivity_reminders_draw_filter(x + 8, body_y + 138, "F4", "PLAN", ProductivityReminderFilter::scheduled);
    productivity_reminders_draw_filter(x + 8, body_y + 174, "F5", "DONE", ProductivityReminderFilter::done);
    draw_text(x + 12, body_y + 229, "A  NEW", current_theme.accent, 1);

    const int32_t list_x = x + 108;
    fill_round_rect(list_x, body_y, 238, 246, 8, current_theme.surface_low);
    for (uint8_t row = 0U; row < productivity_agenda_visible_rows; ++row) {
        const uint8_t item_index = static_cast<uint8_t>(productivity_agenda_scroll + row);
        if (item_index >= productivity_agenda_count) break;
        const ProductivityAgendaItem& item = productivity_agenda[item_index];
        const int32_t row_y = body_y + 18 + static_cast<int32_t>(row * 27U);
        if (item_index == productivity_agenda_focus) fill_round_rect(list_x + 7, row_y - 9, 224, 23, 6, current_theme.accent_soft);
        draw_text(list_x + 14, row_y, item.done ? "[x]" : (item.kind == ProductivityAgendaKind::event ? "[E]" : "[ ]"),
            item.done ? current_theme.success : (item.overdue ? current_theme.warning : current_theme.text), 1);
        char title[19]{}; string_copy(title, item.title, sizeof(title));
        draw_text(list_x + 47, row_y, title, item.done ? current_theme.text_muted : current_theme.text, 1);
        const ProductivityReminderRepeat repeat = productivity_agenda_item_repeat(item);
        if (repeat != ProductivityReminderRepeat::none) {
            char repeat_badge[2]{productivity_reminder_repeat_code(repeat), 0};
            draw_text(list_x + 188, row_y, repeat_badge, current_theme.accent, 1);
        }
        char time[6]{}; productivity_time_text(time, item.hour, item.minute);
        draw_text_right(list_x + 224, row_y, time, item.overdue ? current_theme.warning : current_theme.text_muted, 1);
    }
    if (!productivity_agenda_count) {
        draw_text(list_x + 18, body_y + 86, "NOTHING HERE", current_theme.text_muted, 1);
        draw_text(list_x + 18, body_y + 108, "PRESS A TO CAPTURE", current_theme.text_muted, 1);
    }

    const int32_t detail_x = x + 354;
    fill_round_rect(detail_x, body_y, 126, 246, 8, current_theme.surface_high);
    draw_text(detail_x + 12, body_y + 18, "DETAILS", current_theme.text_muted, 1);
    if (productivity_agenda_count) {
        const ProductivityAgendaItem& item = productivity_agenda[productivity_agenda_focus];
        draw_text(detail_x + 12, body_y + 46, productivity_agenda_kind_text(item.kind), current_theme.accent, 1);
        char date[11]{}; productivity_date_text(date, item.year, item.month, item.day);
        char time[6]{}; productivity_time_text(time, item.hour, item.minute);
        draw_text(detail_x + 12, body_y + 78, "DATE", current_theme.text_muted, 1);
        draw_text(detail_x + 12, body_y + 98, date, item.overdue ? current_theme.warning : current_theme.text, 1);
        draw_text(detail_x + 12, body_y + 126, "TIME", current_theme.text_muted, 1);
        draw_text(detail_x + 12, body_y + 146, time, current_theme.text, 1);
        draw_text(detail_x + 12, body_y + 174, "REPEAT", current_theme.text_muted, 1);
        draw_text(detail_x + 12, body_y + 194, productivity_reminder_repeat_text(productivity_agenda_item_repeat(item)),
            productivity_agenda_item_repeat(item) == ProductivityReminderRepeat::none ? current_theme.text_muted : current_theme.accent, 1);
        draw_text(detail_x + 12, body_y + 224, item.done ? "COMPLETED" : (item.overdue ? "OVERDUE" : "ACTIVE"),
            item.done ? current_theme.success : (item.overdue ? current_theme.warning : current_theme.text), 1);
    }
    draw_text(x, y + 316, "ENTER COMPLETE   O OPEN SOURCE   DEL REMOVE   ESC CLOSE", current_theme.text_muted, 1);
}

void productivity_reminder_render_banner() {
    if (!productivity_reminder_banner_active) return;
    const int32_t x = static_cast<int32_t>(width) - 354;
    const int32_t y = 48;
    fill_round_rect(x, y, 330, 66, 10, current_theme.surface_high);
    draw_text(x + 14, y + 17, "REMINDER DUE", current_theme.warning, 1);
    draw_text_right(x + 314, y + 17, productivity_reminder_repeat_text(productivity_reminder_banner_repeat), current_theme.accent, 1);
    char title[36]{}; string_copy(title, productivity_reminder_banner_title, sizeof(title));
    draw_text(x + 14, y + 43, title, current_theme.text, 1);
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
    productivity_reminder_add_repeat = ProductivityReminderRepeat::none;
    productivity_close();
}
