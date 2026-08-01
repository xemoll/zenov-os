bool productivity_reminder_add_month(ProductivityReminder& reminder) {
    const uint32_t month_index = static_cast<uint32_t>(reminder.year - 2000U) * 12U + static_cast<uint32_t>(reminder.month - 1U);
    if (month_index >= 1199U) return false;
    const uint32_t next = month_index + 1U;
    reminder.year = static_cast<uint16_t>(2000U + next / 12U);
    reminder.month = static_cast<uint8_t>(next % 12U + 1U);
    const uint8_t maximum_day = productivity_days_in_month(reminder.year, reminder.month);
    const uint8_t anchor_day = reminder.repeat_day ? reminder.repeat_day : reminder.day;
    reminder.day = anchor_day > maximum_day ? maximum_day : anchor_day;
    return true;
}

bool productivity_reminder_advance_repeat(ProductivityReminder& reminder) {
    if (reminder.repeat == ProductivityReminderRepeat::none) return false;
    uint16_t now_year = 2026U; uint8_t now_month = 1U, now_day = 1U, now_hour = 0U, now_minute = 0U;
    if (!productivity_utility_now(now_year, now_month, now_day, now_hour, now_minute)) return false;

    if (reminder.repeat == ProductivityReminderRepeat::daily || reminder.repeat == ProductivityReminderRepeat::weekly) {
        const uint32_t interval = reminder.repeat == ProductivityReminderRepeat::daily ? 1U : 7U;
        const uint32_t current_ordinal = productivity_utility_date_ordinal(reminder.year, reminder.month, reminder.day);
        const uint32_t maximum_ordinal = productivity_utility_date_ordinal(2099U, 12U, 31U);
        if (current_ordinal > maximum_ordinal - interval) return false;
        productivity_utility_add_minutes(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute,
            static_cast<int32_t>(interval * 1440U));
        if (productivity_utility_compare_datetime(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute,
            now_year, now_month, now_day, now_hour, now_minute) <= 0) {
            const uint32_t due_ordinal = productivity_utility_date_ordinal(reminder.year, reminder.month, reminder.day);
            const uint32_t now_ordinal = productivity_utility_date_ordinal(now_year, now_month, now_day);
            const uint32_t extra_intervals = (now_ordinal - due_ordinal) / interval + 1U;
            const uint32_t extra_days = extra_intervals * interval;
            if (extra_days > maximum_ordinal || due_ordinal > maximum_ordinal - extra_days) return false;
            productivity_utility_add_minutes(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute,
                static_cast<int32_t>(extra_days * 1440U));
        }
        return true;
    }

    if (!productivity_reminder_add_month(reminder)) return false;
    for (uint16_t guard = 0U; guard < 1200U; ++guard) {
        if (productivity_utility_compare_datetime(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute,
            now_year, now_month, now_day, now_hour, now_minute) > 0) return true;
        if (!productivity_reminder_add_month(reminder)) return false;
    }
    return false;
}

bool productivity_reminders_add_quick() {
    if (productivity_reminder_count >= productivity_reminder_capacity) return false;
    ProductivityReminder reminder{};
    if (!productivity_reminder_parse_quick(productivity_reminder_input, productivity_reminder_input_size,
        productivity_reminder_add_repeat, reminder)) {
        serial::line("UI_REMINDER_QUICK_PARSE_REJECTED");
        return false;
    }
    productivity_reminders[productivity_reminder_count++] = reminder;
    if (!productivity_reminders_save()) {
        --productivity_reminder_count;
        memset(&productivity_reminders[productivity_reminder_count], 0, sizeof(ProductivityReminder));
        return false;
    }
    productivity_reminder_adding = false;
    productivity_reminder_input_size = 0U;
    productivity_reminder_input[0] = 0;
    productivity_reminder_add_repeat = ProductivityReminderRepeat::none;
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
    const ProductivityReminder previous = reminder;
    const bool recurring_completion = !reminder.done && reminder.repeat != ProductivityReminderRepeat::none;
    bool exhausted = false;
    if (recurring_completion) {
        if (!productivity_reminder_advance_repeat(reminder)) {
            reminder.repeat = ProductivityReminderRepeat::none;
            reminder.done = true;
            exhausted = true;
        } else reminder.done = false;
    } else reminder.done = !reminder.done;
    reminder.notified = false;
    if (!productivity_reminders_save()) { reminder = previous; return false; }
    productivity_agenda_rebuild();
    if (exhausted) serial::line("UI_REMINDER_REPEAT_EXHAUSTED");
    else if (recurring_completion) serial::line("UI_REMINDER_REPEAT_ADVANCE_OK");
    else serial::line("UI_REMINDER_TOGGLE_OK");
    return true;
}

bool productivity_reminders_cycle_selected_repeat() {
    if (!productivity_agenda_count) return false;
    const ProductivityAgendaItem item = productivity_agenda[productivity_agenda_focus];
    if (item.kind != ProductivityAgendaKind::reminder || item.source_index >= productivity_reminder_count) return false;
    ProductivityReminder& reminder = productivity_reminders[item.source_index];
    const ProductivityReminderRepeat previous = reminder.repeat;
    reminder.repeat = productivity_reminder_repeat_next(reminder.repeat);
    reminder.repeat_day = reminder.day;
    reminder.notified = false;
    if (!productivity_reminders_save()) { reminder.repeat = previous; return false; }
    productivity_agenda_rebuild();
    serial::line("UI_REMINDER_REPEAT_CHANGE_OK");
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
        productivity_reminder_banner_repeat = reminder.repeat;
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
