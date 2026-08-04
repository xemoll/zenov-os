bool productivity_reminders_handle_key(uint16_t key) {
    if (productivity_reminder_adding) {
        if (key == key_escape) {
            productivity_reminder_adding = false;
            productivity_reminder_input_size = 0U;
            productivity_reminder_input[0] = 0;
            productivity_reminder_add_repeat = ProductivityReminderRepeat::none;
        } else if (key == '\t') {
            productivity_reminder_add_repeat = productivity_reminder_repeat_next(productivity_reminder_add_repeat);
        } else if (key == '\b' && productivity_reminder_input_size) {
            productivity_reminder_input[--productivity_reminder_input_size] = 0;
        } else if (key == '\n') {
            (void)productivity_reminders_add_quick();
        } else if (key >= 32U && key <= 126U && key != '|' &&
                   productivity_reminder_input_size + 1U < sizeof(productivity_reminder_input)) {
            productivity_reminder_input[productivity_reminder_input_size++] = static_cast<char>(key);
            productivity_reminder_input[productivity_reminder_input_size] = 0;
        }
        return true;
    }

    if (key == key_escape) { productivity_utility_close(); return true; }
    if (key == key_f1) productivity_reminder_filter = ProductivityReminderFilter::today;
    else if (key == key_f2) productivity_reminder_filter = ProductivityReminderFilter::overdue;
    else if (key == key_f3) productivity_reminder_filter = ProductivityReminderFilter::week;
    else if (key == key_f4) productivity_reminder_filter = ProductivityReminderFilter::scheduled;
    else if (key == key_f5) productivity_reminder_filter = ProductivityReminderFilter::done;
    else if (key == 'a' || key == 'A') {
        productivity_reminder_adding = true;
        productivity_reminder_input_size = 0U;
        productivity_reminder_input[0] = 0;
        productivity_reminder_add_repeat = ProductivityReminderRepeat::none;
        return true;
    } else if ((key == key_up || key == key_left) && productivity_agenda_count) {
        productivity_agenda_focus = productivity_agenda_focus == 0U ? static_cast<uint8_t>(productivity_agenda_count - 1U) : static_cast<uint8_t>(productivity_agenda_focus - 1U);
    } else if ((key == key_down || key == key_right || key == '\t') && productivity_agenda_count) {
        productivity_agenda_focus = static_cast<uint8_t>((productivity_agenda_focus + 1U) % productivity_agenda_count);
    } else if ((key == '\n' || key == ' ') && productivity_agenda_count) {
        (void)productivity_reminders_toggle_selected();
        return true;
    } else if ((key == 'r' || key == 'R') && productivity_agenda_count) {
        (void)productivity_reminders_cycle_selected_repeat();
        return true;
    } else if ((key == 's' || key == 'S') && productivity_agenda_count) {
        (void)productivity_reminders_snooze_selected();
        return true;
    } else if ((key == 'o' || key == 'O') && productivity_agenda_count) {
        productivity_reminders_open_source();
        return true;
    } else if (key == key_delete && productivity_agenda_count) {
        (void)productivity_reminders_delete_selected();
        return true;
    } else return true;

    productivity_agenda_rebuild();
    return true;
}
