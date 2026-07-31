bool productivity_reminders_add_quick() {
    if (!productivity_reminder_title_valid(productivity_reminder_input, productivity_reminder_input_size) ||
        productivity_reminder_count >= productivity_reminder_capacity) return false;
    ProductivityReminder& reminder = productivity_reminders[productivity_reminder_count];
    memset(&reminder, 0, sizeof(reminder));
    string_copy(reminder.title, productivity_reminder_input, sizeof(reminder.title));
    if (!productivity_utility_now(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute)) return false;
    productivity_utility_add_minutes(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute, 1);
    ++productivity_reminder_count;
    if (!productivity_reminders_save()) { --productivity_reminder_count; return false; }
    productivity_reminder_adding = false;
    productivity_reminder_input_size = 0U;
    productivity_reminder_input[0] = 0;
    productivity_agenda_rebuild();
    serial::line("UI_REMINDER_ADD_OK");
    return true;
}

bool productivity_reminders_toggle_selected() {
    if (!productivity_agenda_count) return false;
    const ProductivityAgendaItem item = productivity_agenda[productivity_agenda_focus];
    if (item.kind == ProductivityAgendaKind::task) {
        if (item.source_index >= productivity_task_count) return false;
        const ProductivityTask selected = productivity_tasks[item.source_index];
        char data[productivity_note_buffer_capacity]{};
        uint32_t size = 0U;
        if (!storage::security_read_file(selected.source_path, reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) return false;
        const uint32_t offset = selected.state_offset;
        if (offset < 3U || offset + 1U >= size || data[offset - 3U] != '-' || data[offset - 2U] != ' ' ||
            data[offset - 1U] != '[' || data[offset + 1U] != ']' ||
            (data[offset] != ' ' && data[offset] != 'x' && data[offset] != 'X')) return false;
        data[offset] = data[offset] == ' ' ? 'x' : ' ';
        if (!storage::guarded_write_file(selected.source_path, reinterpret_cast<const uint8_t*>(data), size, false)) return false;
        productivity_agenda_rebuild();
        serial::line("UI_AGENDA_TASK_TOGGLE_OK");
        return true;
    }
    if (item.kind != ProductivityAgendaKind::reminder || item.source_index >= productivity_reminder_count) return false;
    ProductivityReminder& reminder = productivity_reminders[item.source_index];
    const bool previous = reminder.done;
    reminder.done = !reminder.done;
    reminder.notified = false;
    if (!productivity_reminders_save()) { reminder.done = previous; return false; }
    productivity_agenda_rebuild();
    serial::line("UI_REMINDER_TOGGLE_OK");
    return true;
}

bool productivity_reminders_delete_selected() {
    if (!productivity_agenda_count) return false;
    const ProductivityAgendaItem item = productivity_agenda[productivity_agenda_focus];
    if (item.kind != ProductivityAgendaKind::reminder || item.source_index >= productivity_reminder_count) return false;
    const uint8_t removed = item.source_index;
    const ProductivityReminder backup = productivity_reminders[removed];
    for (uint8_t index = removed; index + 1U < productivity_reminder_count; ++index) productivity_reminders[index] = productivity_reminders[index + 1U];
    --productivity_reminder_count;
    memset(&productivity_reminders[productivity_reminder_count], 0, sizeof(ProductivityReminder));
    if (!productivity_reminders_save()) {
        for (uint8_t index = productivity_reminder_count; index > removed; --index) productivity_reminders[index] = productivity_reminders[index - 1U];
        productivity_reminders[removed] = backup;
        ++productivity_reminder_count;
        return false;
    }
    productivity_agenda_rebuild();
    serial::line("UI_REMINDER_DELETE_OK");
    return true;
}

bool productivity_reminders_snooze_selected() {
    if (!productivity_agenda_count) return false;
    const ProductivityAgendaItem item = productivity_agenda[productivity_agenda_focus];
    if (item.kind != ProductivityAgendaKind::reminder || item.source_index >= productivity_reminder_count) return false;
    ProductivityReminder& reminder = productivity_reminders[item.source_index];
    const ProductivityReminder previous = reminder;
    productivity_utility_add_minutes(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute, 10);
    reminder.done = false;
    reminder.notified = false;
    if (!productivity_reminders_save()) { reminder = previous; return false; }
    productivity_agenda_rebuild();
    serial::line("UI_REMINDER_SNOOZE_OK");
    return true;
}

void productivity_reminders_check_due() {
    uint16_t year = 2026U; uint8_t month = 1U, day = 1U, hour = 0U, minute = 0U;
    if (!productivity_utility_now(year, month, day, hour, minute)) return;
    for (uint8_t index = 0U; index < productivity_reminder_count; ++index) {
        ProductivityReminder& reminder = productivity_reminders[index];
        if (reminder.done || reminder.notified) continue;
        if (productivity_utility_compare_datetime(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute,
            year, month, day, hour, minute) > 0) continue;
        reminder.notified = true;
        productivity_reminder_banner_active = true;
        productivity_reminder_banner_started = timer_ticks;
        string_copy(productivity_reminder_banner_title, reminder.title, sizeof(productivity_reminder_banner_title));
        serial::line("UI_REMINDER_ALARM_DUE");
        break;
    }
    if (productivity_reminder_banner_active && timer_ticks - productivity_reminder_banner_started > 1200U)
        productivity_reminder_banner_active = false;
}

void productivity_reminders_open_source() {
    if (!productivity_agenda_count) return;
    const ProductivityAgendaItem item = productivity_agenda[productivity_agenda_focus];
    if (item.kind == ProductivityAgendaKind::task && item.source_index < productivity_task_count) {
        char path[48]{};
        string_copy(path, productivity_tasks[item.source_index].source_path, sizeof(path));
        productivity_utility_app = ProductivityUtilityApp::none;
        productivity_open(ProductivityApp::notes);
        (void)productivity_notes_open_path(path);
    } else if (item.kind == ProductivityAgendaKind::event) {
        productivity_utility_app = ProductivityUtilityApp::none;
        productivity_open(ProductivityApp::calendar);
        productivity_calendar_year = item.year;
        productivity_calendar_month = item.month;
        productivity_calendar_day = item.day;
    }
}
