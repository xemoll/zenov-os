uint16_t productivity_notes_v2_total_lines() {
    uint16_t lines = 1U;
    for (uint32_t position = 0U; position < productivity_note_size; ++position)
        if (productivity_note_text[position] == '\n' && lines != 0xFFFFU) ++lines;
    return lines;
}

void productivity_notes_v2_render_editor() {
    productivity_notes_v2_bind();
    productivity_notes_v2_ensure_visible();
    draw_window_chrome("ZEN NOTES", "CURSOR MARKDOWN EDITOR");
    const int32_t x = window_x + 16;
    const int32_t y = window_y + 62;
    fill_round_rect(x, y, 480, 34, 8, current_theme.surface_low);
    draw_text(x + 12, y + 13, productivity_note_path, current_theme.accent, 1);
    draw_text_right(x + 462, y + 13, productivity_note_dirty ? "UNSAVED" : "SAVED",
        productivity_note_dirty ? current_theme.warning : current_theme.success, 1);

    fill_round_rect(x, y + 44, 480, 224, 8, current_theme.terminal);
    fill_rect(x + 34, y + 50, 1, 204, current_theme.border);
    const uint16_t cursor_line = productivity_notes_v2_cursor_line();
    const uint16_t cursor_column = productivity_notes_v2_cursor_column();
    const uint16_t total_lines = productivity_notes_v2_total_lines();
    const uint16_t first_line = productivity_notes_v2_scroll_line;

    for (uint8_t row = 0U; row < productivity_notes_v2_visible_rows; ++row) {
        const uint16_t logical_line = static_cast<uint16_t>(first_line + row);
        if (logical_line >= total_lines) break;
        const int32_t row_y = y + 58 + static_cast<int32_t>(row) * 11;
        if (logical_line == cursor_line) fill_rect(x + 36, row_y - 2, 438, 10, current_theme.surface_high);
        draw_uint(x + 8, row_y, static_cast<uint32_t>(logical_line + 1U),
            logical_line == cursor_line ? current_theme.accent : current_theme.text_muted);

        const uint16_t start = productivity_notes_v2_offset_for_line(logical_line);
        const uint16_t end = productivity_notes_v2_line_end(start);
        uint32_t position = static_cast<uint32_t>(start) + productivity_notes_v2_scroll_column;
        if (position > end) position = end;
        char line[productivity_notes_v2_visible_columns + 1U]{};
        uint8_t length = 0U;
        while (position < end && length < productivity_notes_v2_visible_columns) {
            const char value = productivity_note_text[position++];
            line[length++] = value >= 32 && value <= 126 ? value : '?';
        }
        line[length] = 0;
        draw_text(x + 42, row_y, line, current_theme.text, 1);
    }

    if (cursor_line >= first_line && cursor_line < static_cast<uint16_t>(first_line + productivity_notes_v2_visible_rows) &&
        cursor_column >= productivity_notes_v2_scroll_column &&
        cursor_column < static_cast<uint16_t>(productivity_notes_v2_scroll_column + productivity_notes_v2_visible_columns)) {
        const int32_t cursor_x = x + 42 + static_cast<int32_t>(cursor_column - productivity_notes_v2_scroll_column) * 6;
        const int32_t cursor_y = y + 56 + static_cast<int32_t>(cursor_line - first_line) * 11;
        fill_rect(cursor_x, cursor_y, 2, 11, current_theme.accent);
    }

    draw_text(x, y + 286, "ARROWS MOVE  HOME/END LINE  BACKSPACE/DELETE EDIT  F2 SAVE", current_theme.text_muted, 1);
    draw_text(x, y + 306, "F5 UNDO  F6 REDO  TAB 4 SPACES  ESC VAULT", current_theme.text_muted, 1);
    draw_text(x, y + 330, "LN", current_theme.text_muted, 1);
    draw_uint(x + 18, y + 330, static_cast<uint32_t>(cursor_line + 1U), current_theme.text);
    draw_text(x + 66, y + 330, "COL", current_theme.text_muted, 1);
    draw_uint(x + 92, y + 330, static_cast<uint32_t>(cursor_column + 1U), current_theme.text);
    draw_text(x + 146, y + 330, "BYTES", current_theme.text_muted, 1);
    draw_uint(x + 184, y + 330, productivity_note_size, current_theme.text);
    draw_text(x + 250, y + 330, "WORDS", current_theme.text_muted, 1);
    draw_uint(x + 290, y + 330, productivity_note_word_count(), current_theme.text);
    draw_text(x + 346, y + 330, "U", current_theme.text_muted, 1);
    draw_uint(x + 358, y + 330, productivity_notes_v2_undo_count, current_theme.accent);
    draw_text(x + 386, y + 330, "R", current_theme.text_muted, 1);
    draw_uint(x + 398, y + 330, productivity_notes_v2_redo_count, current_theme.accent);
}
