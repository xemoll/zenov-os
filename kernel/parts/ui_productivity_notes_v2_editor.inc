bool productivity_notes_v2_is_active() {
    return productivity_overlay_open && productivity_note_editing && productivity_app == ProductivityApp::notes &&
        !productivity_tasks_open && productivity_utility_app == ProductivityUtilityApp::none;
}

void productivity_notes_v2_ensure_visible();

void productivity_notes_v2_history_push(ProductivityNotesV2Edit* stack, uint8_t& count,
                                        const ProductivityNotesV2Edit& edit) {
    if (count >= productivity_notes_v2_history_capacity) {
        for (uint8_t index = 1U; index < productivity_notes_v2_history_capacity; ++index)
            stack[index - 1U] = stack[index];
        count = static_cast<uint8_t>(productivity_notes_v2_history_capacity - 1U);
    }
    stack[count++] = edit;
}

void productivity_notes_v2_clear_history() {
    productivity_notes_v2_undo_count = 0U;
    productivity_notes_v2_redo_count = 0U;
}

bool productivity_notes_v2_bytes_match(uint16_t position, const char* bytes, uint8_t count) {
    if (!bytes || !count || position > productivity_note_size || count > productivity_note_size - position) return false;
    for (uint8_t index = 0U; index < count; ++index)
        if (productivity_note_text[position + index] != bytes[index]) return false;
    return true;
}

bool productivity_notes_v2_apply_insert(uint16_t position, const char* bytes, uint8_t count) {
    if (!bytes || !count || count > productivity_notes_v2_tab_width || position > productivity_note_size ||
        productivity_note_size + count >= productivity_note_buffer_capacity) return false;
    const uint32_t tail = productivity_note_size - position;
    memmove(productivity_note_text + position + count, productivity_note_text + position, tail + 1U);
    memcpy(productivity_note_text + position, bytes, count);
    productivity_note_size += count;
    productivity_note_text[productivity_note_size] = 0;
    return true;
}

bool productivity_notes_v2_apply_erase(uint16_t position, const char* expected, uint8_t count) {
    if (!count || count > productivity_notes_v2_tab_width || position > productivity_note_size ||
        count > productivity_note_size - position) return false;
    if (expected && !productivity_notes_v2_bytes_match(position, expected, count)) return false;
    const uint32_t tail = productivity_note_size - position - count;
    memmove(productivity_note_text + position, productivity_note_text + position + count, tail + 1U);
    productivity_note_size -= count;
    productivity_note_text[productivity_note_size] = 0;
    return true;
}

void productivity_notes_v2_finish_edit(const ProductivityNotesV2Edit& edit) {
    productivity_notes_v2_history_push(productivity_notes_v2_undo, productivity_notes_v2_undo_count, edit);
    productivity_notes_v2_redo_count = 0U;
    productivity_note_dirty = edit.dirty_after;
    productivity_notes_v2_cursor = edit.cursor_after <= productivity_note_size ? edit.cursor_after : static_cast<uint16_t>(productivity_note_size);
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_ensure_visible();
}

uint16_t productivity_notes_v2_line_start(uint16_t offset) {
    uint32_t position = offset <= productivity_note_size ? offset : productivity_note_size;
    while (position > 0U && productivity_note_text[position - 1U] != '\n') --position;
    return static_cast<uint16_t>(position);
}

uint16_t productivity_notes_v2_line_end(uint16_t offset) {
    uint32_t position = offset <= productivity_note_size ? offset : productivity_note_size;
    while (position < productivity_note_size && productivity_note_text[position] != '\n') ++position;
    return static_cast<uint16_t>(position);
}

uint16_t productivity_notes_v2_cursor_column() {
    const uint16_t start = productivity_notes_v2_line_start(productivity_notes_v2_cursor);
    return static_cast<uint16_t>(productivity_notes_v2_cursor - start);
}

uint16_t productivity_notes_v2_cursor_line() {
    uint16_t line = 0U;
    const uint32_t bounded_cursor = productivity_notes_v2_cursor <= productivity_note_size ? productivity_notes_v2_cursor : productivity_note_size;
    for (uint32_t position = 0U; position < bounded_cursor; ++position)
        if (productivity_note_text[position] == '\n' && line != 0xFFFFU) ++line;
    return line;
}

uint16_t productivity_notes_v2_offset_for_line(uint16_t line) {
    if (!line) return 0U;
    uint16_t current = 0U;
    for (uint32_t position = 0U; position < productivity_note_size; ++position) {
        if (productivity_note_text[position] != '\n') continue;
        if (++current == line) return static_cast<uint16_t>(position + 1U);
    }
    return static_cast<uint16_t>(productivity_note_size);
}

void productivity_notes_v2_ensure_visible() {
    const uint16_t line = productivity_notes_v2_cursor_line();
    const uint16_t column = productivity_notes_v2_cursor_column();
    if (line < productivity_notes_v2_scroll_line) productivity_notes_v2_scroll_line = line;
    else if (line >= static_cast<uint16_t>(productivity_notes_v2_scroll_line + productivity_notes_v2_visible_rows))
        productivity_notes_v2_scroll_line = static_cast<uint16_t>(line - productivity_notes_v2_visible_rows + 1U);
    if (column < productivity_notes_v2_scroll_column) productivity_notes_v2_scroll_column = column;
    else if (column >= static_cast<uint16_t>(productivity_notes_v2_scroll_column + productivity_notes_v2_visible_columns))
        productivity_notes_v2_scroll_column = static_cast<uint16_t>(column - productivity_notes_v2_visible_columns + 1U);
}

void productivity_notes_v2_bind() {
    if (!productivity_notes_v2_is_active()) return;
    if (productivity_notes_v2_bound && string_equal(productivity_notes_v2_bound_path, productivity_note_path)) return;
    productivity_notes_v2_bound = true;
    string_copy(productivity_notes_v2_bound_path, productivity_note_path, sizeof(productivity_notes_v2_bound_path));
    productivity_notes_v2_cursor = static_cast<uint16_t>(productivity_note_size < productivity_note_buffer_capacity ? productivity_note_size : productivity_note_buffer_capacity - 1U);
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_scroll_line = 0U;
    productivity_notes_v2_scroll_column = 0U;
    productivity_notes_v2_clear_history();
    productivity_notes_v2_ensure_visible();
    serial::line("UI_NOTES_V2_OPEN_OK");
}

void productivity_notes_v2_unbind() {
    productivity_notes_v2_bound = false;
    productivity_notes_v2_bound_path[0] = 0;
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_scroll_line = 0U;
    productivity_notes_v2_scroll_column = 0U;
    productivity_notes_v2_clear_history();
}

bool productivity_notes_v2_insert_bytes(const char* bytes, uint8_t count) {
    if (!bytes || !count || count > productivity_notes_v2_tab_width || productivity_notes_v2_cursor > productivity_note_size ||
        productivity_note_size + count >= productivity_note_buffer_capacity) {
        serial::line("UI_NOTES_V2_CAPACITY_REJECTED");
        return false;
    }
    ProductivityNotesV2Edit edit{};
    edit.kind = ProductivityNotesV2EditKind::insert;
    edit.position = productivity_notes_v2_cursor;
    edit.cursor_before = productivity_notes_v2_cursor;
    edit.cursor_after = static_cast<uint16_t>(productivity_notes_v2_cursor + count);
    edit.count = count;
    memcpy(edit.bytes, bytes, count);
    edit.dirty_before = productivity_note_dirty;
    edit.dirty_after = true;
    if (!productivity_notes_v2_apply_insert(edit.position, edit.bytes, edit.count)) {
        serial::line("UI_NOTES_V2_EDIT_REJECTED");
        return false;
    }
    productivity_notes_v2_finish_edit(edit);
    return true;
}

bool productivity_notes_v2_insert_char(char value) {
    return productivity_notes_v2_insert_bytes(&value, 1U);
}

bool productivity_notes_v2_insert_tab() {
    static constexpr char spaces[productivity_notes_v2_tab_width] = {' ', ' ', ' ', ' '};
    return productivity_notes_v2_insert_bytes(spaces, productivity_notes_v2_tab_width);
}

bool productivity_notes_v2_backspace() {
    if (!productivity_notes_v2_cursor || !productivity_note_size) return false;
    ProductivityNotesV2Edit edit{};
    edit.kind = ProductivityNotesV2EditKind::erase;
    edit.position = static_cast<uint16_t>(productivity_notes_v2_cursor - 1U);
    edit.cursor_before = productivity_notes_v2_cursor;
    edit.cursor_after = edit.position;
    edit.count = 1U;
    edit.bytes[0] = productivity_note_text[edit.position];
    edit.dirty_before = productivity_note_dirty;
    edit.dirty_after = true;
    if (!productivity_notes_v2_apply_erase(edit.position, edit.bytes, edit.count)) {
        serial::line("UI_NOTES_V2_EDIT_REJECTED");
        return false;
    }
    productivity_notes_v2_finish_edit(edit);
    return true;
}

bool productivity_notes_v2_delete_forward() {
    if (productivity_notes_v2_cursor >= productivity_note_size) return false;
    ProductivityNotesV2Edit edit{};
    edit.kind = ProductivityNotesV2EditKind::erase;
    edit.position = productivity_notes_v2_cursor;
    edit.cursor_before = productivity_notes_v2_cursor;
    edit.cursor_after = productivity_notes_v2_cursor;
    edit.count = 1U;
    edit.bytes[0] = productivity_note_text[edit.position];
    edit.dirty_before = productivity_note_dirty;
    edit.dirty_after = true;
    if (!productivity_notes_v2_apply_erase(edit.position, edit.bytes, edit.count)) {
        serial::line("UI_NOTES_V2_EDIT_REJECTED");
        return false;
    }
    productivity_notes_v2_finish_edit(edit);
    return true;
}

void productivity_notes_v2_move_horizontal(bool right) {
    if (right) {
        if (productivity_notes_v2_cursor < productivity_note_size) ++productivity_notes_v2_cursor;
    } else if (productivity_notes_v2_cursor) --productivity_notes_v2_cursor;
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_ensure_visible();
}

void productivity_notes_v2_move_home() {
    productivity_notes_v2_cursor = productivity_notes_v2_line_start(productivity_notes_v2_cursor);
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_ensure_visible();
}

void productivity_notes_v2_move_end() {
    productivity_notes_v2_cursor = productivity_notes_v2_line_end(productivity_notes_v2_cursor);
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_ensure_visible();
}

void productivity_notes_v2_move_vertical(bool down) {
    const uint16_t current_start = productivity_notes_v2_line_start(productivity_notes_v2_cursor);
    if (!productivity_notes_v2_preferred_valid) {
        productivity_notes_v2_preferred_column = static_cast<uint16_t>(productivity_notes_v2_cursor - current_start);
        productivity_notes_v2_preferred_valid = true;
    }
    uint16_t target_start = current_start;
    if (down) {
        const uint16_t current_end = productivity_notes_v2_line_end(productivity_notes_v2_cursor);
        if (current_end >= productivity_note_size) return;
        target_start = static_cast<uint16_t>(current_end + 1U);
    } else {
        if (!current_start) return;
        target_start = productivity_notes_v2_line_start(static_cast<uint16_t>(current_start - 1U));
    }
    const uint16_t target_end = productivity_notes_v2_line_end(target_start);
    const uint16_t target_length = static_cast<uint16_t>(target_end - target_start);
    const uint16_t target_column = productivity_notes_v2_preferred_column < target_length ? productivity_notes_v2_preferred_column : target_length;
    productivity_notes_v2_cursor = static_cast<uint16_t>(target_start + target_column);
    productivity_notes_v2_ensure_visible();
}

bool productivity_notes_v2_undo_once() {
    if (!productivity_notes_v2_undo_count) return false;
    const ProductivityNotesV2Edit edit = productivity_notes_v2_undo[productivity_notes_v2_undo_count - 1U];
    const bool applied = edit.kind == ProductivityNotesV2EditKind::insert
        ? productivity_notes_v2_apply_erase(edit.position, edit.bytes, edit.count)
        : productivity_notes_v2_apply_insert(edit.position, edit.bytes, edit.count);
    if (!applied) {
        serial::line("UI_NOTES_V2_HISTORY_REJECTED");
        return false;
    }
    --productivity_notes_v2_undo_count;
    productivity_notes_v2_history_push(productivity_notes_v2_redo, productivity_notes_v2_redo_count, edit);
    productivity_notes_v2_cursor = edit.cursor_before <= productivity_note_size ? edit.cursor_before : static_cast<uint16_t>(productivity_note_size);
    productivity_note_dirty = edit.dirty_before;
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_ensure_visible();
    serial::line("UI_NOTES_V2_UNDO_OK");
    return true;
}

bool productivity_notes_v2_redo_once() {
    if (!productivity_notes_v2_redo_count) return false;
    const ProductivityNotesV2Edit edit = productivity_notes_v2_redo[productivity_notes_v2_redo_count - 1U];
    const bool applied = edit.kind == ProductivityNotesV2EditKind::insert
        ? productivity_notes_v2_apply_insert(edit.position, edit.bytes, edit.count)
        : productivity_notes_v2_apply_erase(edit.position, edit.bytes, edit.count);
    if (!applied) {
        serial::line("UI_NOTES_V2_HISTORY_REJECTED");
        return false;
    }
    --productivity_notes_v2_redo_count;
    productivity_notes_v2_history_push(productivity_notes_v2_undo, productivity_notes_v2_undo_count, edit);
    productivity_notes_v2_cursor = edit.cursor_after <= productivity_note_size ? edit.cursor_after : static_cast<uint16_t>(productivity_note_size);
    productivity_note_dirty = edit.dirty_after;
    productivity_notes_v2_preferred_valid = false;
    productivity_notes_v2_ensure_visible();
    serial::line("UI_NOTES_V2_REDO_OK");
    return true;
}

bool productivity_notes_v2_save() {
    const bool saved = productivity_notes_save();
    if (saved) {
        productivity_notes_v2_clear_history();
        serial::line("UI_NOTES_V2_SAVE_OK");
    }
    return saved;
}

void productivity_notes_v2_close_to_vault() {
    if (productivity_note_dirty) (void)productivity_notes_v2_save();
    productivity_note_editing = false;
    productivity_note_path[0] = 0;
    productivity_notes_reload();
    productivity_notes_v2_unbind();
    serial::line("UI_NOTES_V2_CLOSE_OK");
}
