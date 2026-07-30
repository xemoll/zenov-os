void draw_workspace_launcher_card() {
    if (active_view != View::launcher || (!launcher_item_matches(3U) && !workspace_query_matches())) return;
    const int32_t x = launcher_card_x(3U);
    const int32_t y = launcher_card_y(3U) + ui_scene_offset_y;
    const bool focused = launcher_focus == 3U;
    uint32_t fill = focused ? current_theme.accent_soft : current_theme.surface_low;
    if (!focused && is_hovered(launcher_target(3U))) fill = current_theme.surface_high;
    fill_round_rect(x, y, launcher_card_width, launcher_card_height, 9, fill);
    fill_round_rect(x + 8, y + 7, 28, 28, 7, current_theme.surface_high);
    draw_shell_icon(ShellIcon::apps, x + 10, y + 9, current_theme.accent);
    draw_text(x + 50, y + 15, "Workspace", current_theme.text, 1);
    draw_text_right_ellipsis(x + launcher_card_width - 14, y + 15, 184, "Clock, calendar, notes", focus_text_color(focused), 1);
    if (focused) {
        fill_rect(x + 3, y + 9, 2, 24, current_theme.accent);
        draw_focus_outline(x, y, launcher_card_width, launcher_card_height, current_theme.accent);
    }
}

void draw_chrome(const char* subtitle) {
    const int32_t radius = static_cast<int32_t>(desktop_radius);
    draw_shadow(window_x, window_y, window_width, window_height, radius);
    fill_round_rect_alpha(window_x, window_y, window_width, window_height, radius, current_theme.surface, desktop_opacity);
    draw_round_border(window_x, window_y, window_width, window_height, radius, current_theme.border);
    fill_round_rect(window_x + 1, window_y + 1, window_width - 2, titlebar_height + 3, radius > 1 ? radius - 1 : 0, current_theme.surface_low);
    fill_rect(window_x + 1, window_y + titlebar_height + 1, window_width - 2, 1, current_theme.border);
    fill_round_rect(window_x + 14, window_y + 10, 24, 24, 7, current_theme.accent_soft);
    draw_shell_icon(ShellIcon::apps, window_x + 16, window_y + 12, current_theme.accent);
    draw_text(window_x + 50, window_y + 16, "Workspace", current_theme.text, 1);
    draw_text_right_ellipsis(window_x + window_width - 18, window_y + 16, 160, subtitle, current_theme.text_muted, 1);
}

void draw_sidebar_button(uint8_t index) {
    const int32_t x = window_x + sidebar_x_offset;
    const int32_t y = window_y + 68 + static_cast<int32_t>(index) * 50;
    const bool selected = static_cast<uint8_t>(page) == index;
    fill_round_rect(x, y, sidebar_width, page_button_height, 9, selected ? current_theme.accent_soft : current_theme.surface_low);
    fill_circle_alpha(x + 17, y + 21, 4, selected ? current_theme.accent : current_theme.text_muted, 255);
    draw_text(x + 30, y + 18, page_name(static_cast<Page>(index)), selected ? current_theme.text : current_theme.text_muted, 1);
    if (selected) fill_rect(x + 3, y + 9, 2, 24, current_theme.accent);
}

void draw_sidebar() {
    draw_text(window_x + 18, window_y + 56, "SYSTEM APPS", current_theme.text_muted, 1);
    for (uint8_t i = 0U; i < page_count; ++i) draw_sidebar_button(i);
    fill_rect(window_x + 122, window_y + 56, 1, 316, current_theme.border);
    draw_text(window_x + 18, window_y + 350, "TAB SWITCH", current_theme.text_muted, 1);
    draw_text(window_x + 18, window_y + 368, "ESC CLOSE", current_theme.text_muted, 1);
}

void draw_clock() {
    RtcSnapshot snapshot{};
    const bool valid = rtc_snapshot(snapshot);
    const int32_t x = window_x + content_x_offset;
    draw_text(x, window_y + 68, "Hardware time", current_theme.text, 2);
    draw_text(x, window_y + 92, valid ? "Hardware RTC" : "RTC unavailable", valid ? current_theme.text_muted : current_theme.warning, 1);
    fill_round_rect(x, window_y + 118, content_width, 112, 12, current_theme.terminal);
    if (valid) {
        draw_two_digits(x + 46, window_y + 152, snapshot.hour, current_theme.text);
        draw_char(x + 60, window_y + 152, ':', current_theme.accent, 1);
        draw_two_digits(x + 72, window_y + 152, snapshot.minute, current_theme.text);
        draw_char(x + 86, window_y + 152, ':', current_theme.accent, 1);
        draw_two_digits(x + 98, window_y + 152, snapshot.second, current_theme.text);
        char date[11]; format_date(snapshot.date, date);
        draw_text(x + 46, window_y + 188, date, current_theme.text_muted, 1);
    } else draw_text(x + 46, window_y + 166, "NO RTC SAMPLE", current_theme.warning, 2);
    fill_round_rect(x, window_y + 246, content_width, 72, 10, current_theme.surface_low);
    draw_text(x + 16, window_y + 264, "Session uptime", current_theme.text_muted, 1);
    char uptime[16]; shell_format_uint(timer_ticks / 100U, uptime, sizeof(uptime), " s");
    draw_text_right(x + content_width - 18, window_y + 264, uptime, current_theme.text, 1);
    draw_text(x + 16, window_y + 292, "No timezone database installed", current_theme.text_muted, 1);
}

const char* month_name(uint8_t month) {
    static constexpr const char* names[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    return month >= 1U && month <= 12U ? names[month - 1U] : "Month";
}

void draw_calendar() {
    ensure_today();
    const int32_t x = window_x + content_x_offset;
    draw_text(x, window_y + 68, month_name(selected_date.month), current_theme.text, 2);
    draw_uint(x + 130, window_y + 68, selected_date.year, current_theme.text_muted);
    draw_text_right(x + content_width, window_y + 72, "Arrows day  Home today", current_theme.text_muted, 1);
    static constexpr const char* weekdays[7] = {"MO","TU","WE","TH","FR","SA","SU"};
    for (uint8_t column = 0U; column < 7U; ++column) draw_text(x + 8 + static_cast<int32_t>(column) * 49, window_y + 112, weekdays[column], current_theme.text_muted, 1);
    const uint8_t first = productivity_model::weekday_monday_zero(Date{selected_date.year, selected_date.month, 1U});
    const uint8_t maximum = productivity_model::days_in_month(selected_date.year, selected_date.month);
    for (uint8_t day = 1U; day <= maximum; ++day) {
        const uint8_t cell = static_cast<uint8_t>(first + day - 1U);
        const uint8_t row = static_cast<uint8_t>(cell / 7U), column = static_cast<uint8_t>(cell % 7U);
        const int32_t cell_x = x + static_cast<int32_t>(column) * 49;
        const int32_t cell_y = window_y + 132 + static_cast<int32_t>(row) * 38;
        const bool selected = day == selected_date.day;
        const bool is_today = selected_date.year == today.year && selected_date.month == today.month && day == today.day;
        if (selected) fill_round_rect(cell_x, cell_y, 42, 30, 8, current_theme.accent_soft);
        else if (is_today) draw_round_border(cell_x, cell_y, 42, 30, 8, current_theme.accent);
        draw_uint(cell_x + 13, cell_y + 11, day, selected ? current_theme.text : (is_today ? current_theme.accent : current_theme.text_muted));
    }
    char date[11]; format_date(selected_date, date);
    fill_round_rect(x, window_y + 340, content_width, 34, 8, current_theme.surface_low);
    draw_text(x + 12, window_y + 353, date, current_theme.text, 1);
    draw_text_right(x + content_width - 12, window_y + 353, "Enter opens daily note", current_theme.text_muted, 1);
}

uint32_t editor_cursor_line(const Editor& editor) {
    uint32_t line = 0U;
    for (uint32_t i = 0U; i < editor.cursor && i < editor.length; ++i) if (editor.data[i] == '\n') ++line;
    return line;
}

uint32_t editor_line_start_by_number(const Editor& editor, uint32_t target) {
    uint32_t line = 0U, position = 0U;
    while (position < editor.length && line < target) if (editor.data[position++] == '\n') ++line;
    return position;
}

void editor_ensure_cursor_visible(const Editor& editor) {
    const uint32_t line = editor_cursor_line(editor);
    if (line < editor_scroll_line) editor_scroll_line = line;
    else if (line >= editor_scroll_line + editor_rows) editor_scroll_line = line - editor_rows + 1U;
}

void draw_editor(Editor& editor, DocumentState state, const char* title, const char* path, bool daily) {
    editor_ensure_cursor_visible(editor);
    const int32_t x = window_x + editor_x_offset;
    const int32_t y = window_y + editor_y_offset;
    draw_text(window_x + content_x_offset, window_y + 68, title, current_theme.text, 2);
    draw_text_ellipsis(window_x + content_x_offset, window_y + 91, 240, path, current_theme.text_muted, 1);
    draw_text_right(window_x + content_x_offset + content_width, window_y + 91,
                    document_status(state, editor.dirty), editor.dirty ? current_theme.warning : current_theme.success, 1);
    fill_round_rect(x, y, editor_width, editor_height, 10, current_theme.terminal);
    uint32_t position = editor_line_start_by_number(editor, editor_scroll_line);
    for (uint32_t row = 0U; row < editor_rows; ++row) {
        uint32_t column = 0U;
        while (position < editor.length && editor.data[position] != '\n') {
            if (column < editor_columns) draw_char(x + 12 + static_cast<int32_t>(column) * 6, y + 14 + static_cast<int32_t>(row) * editor_line_height,
                                                   editor.data[position], current_theme.text, 1);
            ++column; ++position;
        }
        if (position < editor.length && editor.data[position] == '\n') ++position;
    }
    const uint32_t cursor_line = editor_cursor_line(editor);
    if (cursor_line >= editor_scroll_line && cursor_line < editor_scroll_line + editor_rows) {
        const uint32_t line_start = productivity_model::editor_line_start(editor, editor.cursor);
        uint32_t column = editor.cursor - line_start;
        if (column > editor_columns) column = editor_columns;
        fill_rect_alpha(x + 12 + static_cast<int32_t>(column) * 6,
                        y + 10 + static_cast<int32_t>(cursor_line - editor_scroll_line) * editor_line_height,
                        1, 9, current_theme.accent, 255);
    }
    fill_round_rect(window_x + content_x_offset, window_y + 348, 90, 28, 7, editor.dirty ? current_theme.accent_soft : current_theme.surface_low);
    draw_text(window_x + content_x_offset + 20, window_y + 359, "F2 SAVE", editor.dirty ? current_theme.text : current_theme.text_muted, 1);
    if (daily) {
        char counts[32] = "LINKS "; uint32_t length = 6U;
        char number[16]; files_uint_text(productivity_model::count_wiki_links(editor), number);
        for (uint32_t i = 0U; number[i] && length + 1U < sizeof(counts); ++i) counts[length++] = number[i];
        static constexpr char tag_text[] = "   TAGS ";
        for (uint32_t i = 0U; tag_text[i] && length + 1U < sizeof(counts); ++i) counts[length++] = tag_text[i];
        files_uint_text(productivity_model::count_tags(editor), number);
        for (uint32_t i = 0U; number[i] && length + 1U < sizeof(counts); ++i) counts[length++] = number[i];
        counts[length] = 0;
        draw_text_right(window_x + content_x_offset + content_width, window_y + 359, counts, current_theme.text_muted, 1);
    } else draw_text_right(window_x + content_x_offset + content_width, window_y + 359, "2047 BYTE LIMIT", current_theme.text_muted, 1);
}

void render() {
    draw_chrome(page_name(page));
    draw_sidebar();
    if (page == Page::clock) draw_clock();
    else if (page == Page::calendar) draw_calendar();
    else if (page == Page::notes) {
        ensure_daily_note(); char path[48]; build_daily_path(selected_date, path);
        char title[32] = "Daily note "; char date[11]; format_date(selected_date, date);
        uint32_t length = static_cast<uint32_t>(string_length(title));
        for (uint32_t i = 0U; date[i] && length + 1U < sizeof(title); ++i) title[length++] = date[i];
        title[length] = 0; draw_editor(daily_editor, daily_state, title, path, true);
    } else { ensure_scratchpad(); draw_editor(scratch_editor, scratch_state, "Scratchpad", "/docs/scratchpad.txt", false); }
}

void render_scene() {
    if (!opened) {
        render_scene_base();
        draw_workspace_launcher_card();
        return;
    }
    if (!backbuffer) return;
    draw_wallpaper(); draw_topbar();
    const int32_t saved_window_y = window_y;
    window_y += ui_scene_offset_y;
    render();
    window_y = saved_window_y;
    draw_taskbar(); draw_quick_settings(); draw_status_center();
}

bool model_contract_ok() {
    Date sample{2026U, 7U, 30U};
    Editor editor{};
    static constexpr uint8_t text[] = {'[','[','A',']',']',' ','#','x'};
    return productivity_model::weekday_monday_zero(sample) == 3U &&
        productivity_model::days_in_month(2024U, 2U) == 29U &&
        productivity_model::editor_load(editor, text, sizeof(text)) &&
        productivity_model::count_wiki_links(editor) == 1U && productivity_model::count_tags(editor) == 1U;
}

} // namespace productivity
