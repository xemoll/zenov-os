bool productivity_tasks_append_quick() {
    if (!productivity_task_input_size || !productivity_ensure_storage()) return false;
    constexpr const char* path = "/notes/tasks.md";
    char data[productivity_note_buffer_capacity]{};
    uint32_t size = 0U;
    productivity_notes_reload();
    if (productivity_tasks_source_exists(path)) {
        if (!storage::security_read_file(path, reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
            serial::line("UI_TASKS_ADD_FAILED");
            return false;
        }
    } else {
        constexpr char header[] = "# Tasks\n\n";
        memcpy(data, header, sizeof(header) - 1U);
        size = sizeof(header) - 1U;
    }
    constexpr char prefix[] = "- [ ] ";
    const uint32_t required = static_cast<uint32_t>(sizeof(prefix) - 1U) + productivity_task_input_size + 1U;
    if (size + required >= sizeof(data)) {
        serial::line("UI_TASKS_ADD_FAILED");
        return false;
    }
    memcpy(data + size, prefix, sizeof(prefix) - 1U);
    size += sizeof(prefix) - 1U;
    memcpy(data + size, productivity_task_input, productivity_task_input_size);
    size += productivity_task_input_size;
    data[size++] = '\n';
    if (!storage::guarded_write_file(path, reinterpret_cast<const uint8_t*>(data), size, false)) {
        serial::line("UI_TASKS_ADD_FAILED");
        return false;
    }
    productivity_task_input_size = 0U;
    productivity_task_input[0] = 0;
    productivity_task_adding = false;
    productivity_tasks_scan();
    serial::line("UI_TASKS_ADD_OK");
    return true;
}

bool productivity_tasks_toggle_selected() {
    if (!productivity_task_visible_count) return false;
    const ProductivityTask selected = productivity_tasks[productivity_task_visible[productivity_task_focus]];
    char data[productivity_note_buffer_capacity]{};
    uint32_t size = 0U;
    if (!storage::security_read_file(selected.source_path, reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
        serial::line("UI_TASKS_TOGGLE_FAILED");
        return false;
    }
    const uint32_t offset = selected.state_offset;
    if (offset < 3U || offset + 1U >= size || data[offset - 3U] != '-' || data[offset - 2U] != ' ' ||
        data[offset - 1U] != '[' || data[offset + 1U] != ']' ||
        (data[offset] != ' ' && data[offset] != 'x' && data[offset] != 'X')) {
        serial::line("UI_TASKS_TOGGLE_FAILED");
        return false;
    }
    data[offset] = data[offset] == ' ' ? 'x' : ' ';
    if (!storage::guarded_write_file(selected.source_path, reinterpret_cast<const uint8_t*>(data), size, false)) {
        serial::line("UI_TASKS_TOGGLE_FAILED");
        return false;
    }
    productivity_tasks_scan();
    serial::line("UI_TASKS_TOGGLE_OK");
    return true;
}

bool productivity_task_is_overdue(const ProductivityTask& task) {
    if (task.done || !task.due_year) return false;
    uint8_t weekday = 0U, current_day = 0U, current_month = 0U, current_year = 0U;
    if (!rtc_read_date(weekday, current_day, current_month, current_year)) return false;
    const uint16_t full_year = static_cast<uint16_t>(2000U + current_year);
    if (task.due_year != full_year) return task.due_year < full_year;
    if (task.due_month != current_month) return task.due_month < current_month;
    return task.due_day < current_day;
}
