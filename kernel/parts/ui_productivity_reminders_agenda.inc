void productivity_agenda_add(ProductivityAgendaKind kind, uint8_t source_index, uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, bool done, bool overdue, const char* title) {
    if (productivity_agenda_count >= productivity_agenda_capacity || !title || !*title) return;
    ProductivityAgendaItem& item = productivity_agenda[productivity_agenda_count++];
    item.kind = kind;
    item.source_index = source_index;
    item.year = year;
    item.month = month;
    item.day = day;
    item.hour = hour;
    item.minute = minute;
    item.done = done;
    item.overdue = overdue;
    string_copy(item.title, title, sizeof(item.title));
}

bool productivity_agenda_filter_match(bool done, int32_t comparison, bool same_day) {
    if (productivity_reminder_filter == ProductivityReminderFilter::done) return done;
    if (done) return false;
    if (productivity_reminder_filter == ProductivityReminderFilter::today) return same_day;
    if (productivity_reminder_filter == ProductivityReminderFilter::overdue) return comparison < 0;
    return comparison >= 0;
}

int32_t productivity_agenda_title_compare(const char* left, const char* right) {
    uint32_t index = 0U;
    while (left[index] && right[index]) {
        if (left[index] < right[index]) return -1;
        if (left[index] > right[index]) return 1;
        ++index;
    }
    if (!left[index] && !right[index]) return 0;
    return left[index] ? 1 : -1;
}

bool productivity_agenda_before(const ProductivityAgendaItem& left, const ProductivityAgendaItem& right) {
    const uint32_t left_stamp = productivity_utility_minute_stamp(left.year, left.month, left.day, left.hour, left.minute);
    const uint32_t right_stamp = productivity_utility_minute_stamp(right.year, right.month, right.day, right.hour, right.minute);
    if (left_stamp != right_stamp) return left_stamp < right_stamp;
    if (left.kind != right.kind) return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
    return productivity_agenda_title_compare(left.title, right.title) < 0;
}

void productivity_agenda_sort() {
    for (uint8_t index = 1U; index < productivity_agenda_count; ++index) {
        const ProductivityAgendaItem value = productivity_agenda[index];
        uint8_t position = index;
        while (position > 0U && productivity_agenda_before(value, productivity_agenda[position - 1U])) {
            productivity_agenda[position] = productivity_agenda[position - 1U];
            --position;
        }
        productivity_agenda[position] = value;
    }
}

void productivity_agenda_rebuild() {
    productivity_agenda_count = 0U;
    memset(productivity_agenda, 0, sizeof(productivity_agenda));
    uint16_t now_year = 2026U; uint8_t now_month = 1U, now_day = 1U, now_hour = 0U, now_minute = 0U;
    (void)productivity_utility_now(now_year, now_month, now_day, now_hour, now_minute);
    for (uint8_t index = 0U; index < productivity_reminder_count; ++index) {
        const ProductivityReminder& reminder = productivity_reminders[index];
        const int32_t comparison = productivity_utility_compare_datetime(reminder.year, reminder.month, reminder.day, reminder.hour, reminder.minute,
            now_year, now_month, now_day, now_hour, now_minute);
        const bool same_day = reminder.year == now_year && reminder.month == now_month && reminder.day == now_day;
        if (productivity_agenda_filter_match(reminder.done, comparison, same_day))
            productivity_agenda_add(ProductivityAgendaKind::reminder, index, reminder.year, reminder.month, reminder.day,
                reminder.hour, reminder.minute, reminder.done, !reminder.done && comparison < 0, reminder.title);
    }
    productivity_tasks_scan();
    for (uint8_t index = 0U; index < productivity_task_count; ++index) {
        const ProductivityTask& task = productivity_tasks[index];
        if (!task.due_year) continue;
        const int32_t comparison = productivity_utility_compare_datetime(task.due_year, task.due_month, task.due_day, 23U, 59U,
            now_year, now_month, now_day, now_hour, now_minute);
        const bool same_day = task.due_year == now_year && task.due_month == now_month && task.due_day == now_day;
        if (productivity_agenda_filter_match(task.done, comparison, same_day))
            productivity_agenda_add(ProductivityAgendaKind::task, index, task.due_year, task.due_month, task.due_day,
                23U, 59U, task.done, !task.done && comparison < 0, task.text);
    }
    productivity_calendar_load();
    if (productivity_reminder_filter != ProductivityReminderFilter::done && productivity_reminder_filter != ProductivityReminderFilter::overdue) {
        for (uint8_t index = 0U; index < productivity_event_count; ++index) {
            const ProductivityEvent& event = productivity_events[index];
            const int32_t comparison = productivity_utility_compare_datetime(event.year, event.month, event.day, 0U, 0U,
                now_year, now_month, now_day, now_hour, now_minute);
            const bool same_day = event.year == now_year && event.month == now_month && event.day == now_day;
            if ((productivity_reminder_filter == ProductivityReminderFilter::today && same_day) ||
                (productivity_reminder_filter == ProductivityReminderFilter::scheduled && comparison >= 0))
                productivity_agenda_add(ProductivityAgendaKind::event, index, event.year, event.month, event.day, 0U, 0U, false, false, event.title);
        }
    }
    productivity_agenda_sort();
    if (!productivity_agenda_count) { productivity_agenda_focus = 0U; productivity_agenda_scroll = 0U; }
    else {
        if (productivity_agenda_focus >= productivity_agenda_count) productivity_agenda_focus = static_cast<uint8_t>(productivity_agenda_count - 1U);
        if (productivity_agenda_focus < productivity_agenda_scroll) productivity_agenda_scroll = productivity_agenda_focus;
        if (productivity_agenda_focus >= productivity_agenda_scroll + productivity_agenda_visible_rows)
            productivity_agenda_scroll = static_cast<uint8_t>(productivity_agenda_focus - productivity_agenda_visible_rows + 1U);
    }
    serial::line("UI_AGENDA_REBUILD_OK");
}
