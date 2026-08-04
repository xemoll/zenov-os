bool productivity_tasks_append_quick() {
    if (!productivity_task_input_size || !productivity_ensure_storage()) return false;
    constexpr const char* path = "/notes/tasks.md";
    char data[productivity_note_buffer_capacity]{};
    uint32_t size = 0U;
    const int32_t existing_index = storage::find_entry(path);
    if (existing_index >= 0) {
        if (storage::zfs_entries[existing_index].type != storage::zfs_type_file ||
            !storage::security_read_file(path, reinterpret_cast<uint8_t*>(data), sizeof(data) - 1U, size)) {
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
    productivity_task_editing = false;
    productivity_tasks_scan();
    serial::line("UI_TASKS_ADD_OK");
    return true;
}

bool productivity_tasks_selected(ProductivityTask& selected) {
    if (!productivity_task_visible_count || productivity_task_focus >= productivity_task_visible_count) return false;
    selected = productivity_tasks[productivity_task_visible[productivity_task_focus]];
    return true;
}

bool productivity_tasks_read_validated(const ProductivityTask& selected, char* data, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (!data || capacity < 2U ||
        !storage::security_read_file(selected.source_path, reinterpret_cast<uint8_t*>(data), capacity - 1U, size)) {
        serial::line("UI_TASKS_MUTATION_FAILED");
        return false;
    }
    data[size] = 0;
    const uint32_t line_start = selected.line_start;
    const uint32_t line_end = selected.line_end;
    const uint32_t payload = selected.payload_offset;
    const uint32_t state = selected.state_offset;
    if (line_start >= line_end || line_end > size || payload >= line_end || state < line_start + 3U || state + 2U >= line_end ||
        data[state - 3U] != '-' || data[state - 2U] != ' ' || data[state - 1U] != '[' || data[state + 1U] != ']' ||
        !productivity_task_tag_boundary(data[state + 2U]) || payload <= state + 2U ||
        (data[state] != ' ' && data[state] != 'x' && data[state] != 'X') ||
        productivity_task_line_hash(data, line_start, line_end) != selected.line_hash) {
        serial::line("UI_TASKS_STALE_REJECTED");
        return false;
    }
    return true;
}

bool productivity_tasks_toggle_selected() {
    ProductivityTask selected{};
    if (!productivity_tasks_selected(selected)) return false;
    char data[productivity_note_buffer_capacity]{};
    uint32_t size = 0U;
    if (!productivity_tasks_read_validated(selected, data, sizeof(data), size)) {
        serial::line("UI_TASKS_TOGGLE_FAILED");
        return false;
    }
    const uint32_t offset = selected.state_offset;
    data[offset] = data[offset] == ' ' ? 'x' : ' ';
    if (!storage::guarded_write_file(selected.source_path, reinterpret_cast<const uint8_t*>(data), size, false)) {
        serial::line("UI_TASKS_TOGGLE_FAILED");
        return false;
    }
    productivity_tasks_scan();
    serial::line("UI_TASKS_TOGGLE_OK");
    return true;
}

bool productivity_tasks_edit_selected() {
    if (!productivity_task_input_size) return false;
    ProductivityTask selected{};
    if (!productivity_tasks_selected(selected)) return false;
    char data[productivity_note_buffer_capacity]{};
    uint32_t size = 0U;
    if (!productivity_tasks_read_validated(selected, data, sizeof(data), size)) {
        serial::line("UI_TASKS_EDIT_FAILED");
        return false;
    }
    const uint32_t payload = selected.payload_offset;
    const uint32_t line_end = selected.line_end;
    const uint32_t old_size = line_end - payload;
    const uint32_t new_size = productivity_task_input_size;
    if (new_size >= productivity_task_text_capacity || size - old_size + new_size >= sizeof(data)) {
        serial::line("UI_TASKS_EDIT_FAILED");
        return false;
    }
    memmove(data + payload + new_size, data + line_end, size - line_end);
    memcpy(data + payload, productivity_task_input, new_size);
    size = size - old_size + new_size;
    data[size] = 0;
    if (!storage::guarded_write_file(selected.source_path, reinterpret_cast<const uint8_t*>(data), size, false)) {
        serial::line("UI_TASKS_EDIT_FAILED");
        return false;
    }
    productivity_task_input_size = 0U;
    productivity_task_input[0] = 0;
    productivity_task_editing = false;
    productivity_tasks_scan();
    serial::line("UI_TASKS_EDIT_OK");
    return true;
}

bool productivity_tasks_delete_selected() {
    ProductivityTask selected{};
    if (!productivity_tasks_selected(selected)) return false;
    char data[productivity_note_buffer_capacity]{};
    uint32_t size = 0U;
    if (!productivity_tasks_read_validated(selected, data, sizeof(data), size)) {
        serial::line("UI_TASKS_DELETE_FAILED");
        return false;
    }
    uint32_t remove_end = selected.line_end;
    if (remove_end < size && data[remove_end] == '\n') ++remove_end;
    const uint32_t removed = remove_end - selected.line_start;
    memmove(data + selected.line_start, data + remove_end, size - remove_end);
    size -= removed;
    data[size] = 0;
    if (!storage::guarded_write_file(selected.source_path, reinterpret_cast<const uint8_t*>(data), size, false)) {
        serial::line("UI_TASKS_DELETE_FAILED");
        return false;
    }
    productivity_tasks_scan();
    serial::line("UI_TASKS_DELETE_OK");
    return true;
}

bool productivity_task_is_overdue(const ProductivityTask& task) {
    return !task.done && productivity_task_relation_to_today(task) < 0;
}
