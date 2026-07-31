void productivity_tasks_sort() {
    for (uint8_t index = 1U; index < productivity_task_count; ++index) {
        ProductivityTask value = productivity_tasks[index];
        uint8_t position = index;
        while (position > 0U && productivity_task_before(value, productivity_tasks[position - 1U])) {
            productivity_tasks[position] = productivity_tasks[position - 1U];
            --position;
        }
        productivity_tasks[position] = value;
    }
}

bool productivity_task_matches_filter(const ProductivityTask& task) {
    if (productivity_task_filter == ProductivityTaskFilter::open) return !task.done;
    if (productivity_task_filter == ProductivityTaskFilter::done) return task.done;
    return true;
}

void productivity_tasks_rebuild_visible() {
    productivity_task_visible_count = 0U;
    for (uint8_t index = 0U; index < productivity_task_count; ++index) {
        if (productivity_task_matches_filter(productivity_tasks[index]))
            productivity_task_visible[productivity_task_visible_count++] = index;
    }
    if (!productivity_task_visible_count) {
        productivity_task_focus = 0U;
        productivity_task_scroll = 0U;
        return;
    }
    if (productivity_task_focus >= productivity_task_visible_count)
        productivity_task_focus = static_cast<uint8_t>(productivity_task_visible_count - 1U);
    if (productivity_task_focus < productivity_task_scroll) productivity_task_scroll = productivity_task_focus;
    if (productivity_task_focus >= productivity_task_scroll + productivity_task_visible_rows)
        productivity_task_scroll = static_cast<uint8_t>(productivity_task_focus - productivity_task_visible_rows + 1U);
}

void productivity_tasks_scan() {
    productivity_task_count = 0U;
    productivity_notes_reload();
    char data[productivity_note_buffer_capacity]{};
    for (uint32_t note_index = 0U; note_index < productivity_note_count && productivity_task_count < productivity_task_capacity; ++note_index) {
        const storage::BrowserEntry& note = productivity_notes[note_index];
        if (note.type != storage::zfs_type_file || !productivity_has_suffix(note.name, ".md")) continue;
        uint32_t size = 0U;
        memset(data, 0, sizeof(data));
        if (!storage::security_read_file(note.path, reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) continue;
        data[size] = 0;
        uint32_t position = 0U;
        uint16_t line_number = 1U;
        while (position < size && productivity_task_count < productivity_task_capacity) {
            const uint32_t line_start = position;
            while (position < size && data[position] != '\n') ++position;
            const uint32_t line_end = position;
            if (position < size) ++position;
            uint32_t marker = line_start;
            while (marker < line_end && (data[marker] == ' ' || data[marker] == '\t')) ++marker;
            if (marker + 5U <= line_end && data[marker] == '-' && data[marker + 1U] == ' ' &&
                data[marker + 2U] == '[' && data[marker + 4U] == ']' &&
                (data[marker + 3U] == ' ' || data[marker + 3U] == 'x' || data[marker + 3U] == 'X')) {
                ProductivityTask& task = productivity_tasks[productivity_task_count++];
                memset(&task, 0, sizeof(task));
                string_copy(task.source_path, note.path, sizeof(task.source_path));
                task.state_offset = static_cast<uint16_t>(marker + 3U);
                task.line_number = line_number;
                task.done = data[marker + 3U] != ' ';
                task.waiting = productivity_task_line_has_token_ci(data, marker + 5U, line_end, "#W");
                task.priority = productivity_task_line_has_token_ci(data, marker + 5U, line_end, "#P1") ? 1U :
                    (productivity_task_line_has_token_ci(data, marker + 5U, line_end, "#P2") ? 2U :
                    (productivity_task_line_has_token_ci(data, marker + 5U, line_end, "#P3") ? 3U : 4U));
                (void)productivity_task_parse_due(data, marker + 5U, line_end, task.due_year, task.due_month, task.due_day);
                uint32_t text_position = marker + 5U;
                while (text_position < line_end && data[text_position] == ' ') ++text_position;
                uint32_t output = 0U;
                while (text_position < line_end && output + 1U < sizeof(task.text)) {
                    const char value = data[text_position++];
                    if (value < 32 || value > 126) continue;
                    task.text[output++] = value;
                }
                task.text[output] = 0;
            }
            if (line_number != 0xFFFFU) ++line_number;
        }
    }
    productivity_tasks_sort();
    productivity_tasks_rebuild_visible();
    serial::line("UI_TASKS_SCAN_OK");
}

bool productivity_tasks_source_exists(const char* path) {
    for (uint32_t index = 0U; index < productivity_note_count; ++index)
        if (string_equal(productivity_notes[index].path, path)) return true;
    return false;
}
