uint8_t productivity_calendar_v2_events_on(uint16_t year, uint8_t month, uint8_t day) {
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < productivity_calendar_v2_count; ++index)
        if (productivity_calendar_v2_occurs_on(productivity_calendar_v2_events[index], year, month, day)) ++count;
    return count;
}

bool productivity_calendar_v2_selected_index(uint8_t& source_index) {
    if (!productivity_calendar_v2_day_count || productivity_calendar_v2_focus >= productivity_calendar_v2_day_count) return false;
    source_index = productivity_calendar_v2_day_indices[productivity_calendar_v2_focus];
    return source_index < productivity_calendar_v2_count;
}

void productivity_calendar_v2_begin_add() {
    if (productivity_calendar_v2_count >= productivity_calendar_v2_capacity) {
        serial::line("UI_CALENDAR_V2_CAPACITY");
        return;
    }
    productivity_calendar_v2_editing = true;
    productivity_calendar_v2_edit_existing = false;
    productivity_calendar_v2_input_size = 0U;
    productivity_calendar_v2_input[0] = 0;
}

void productivity_calendar_v2_begin_edit() {
    uint8_t source_index = 0U;
    if (!productivity_calendar_v2_selected_index(source_index)) return;
    productivity_calendar_v2_editing = true;
    productivity_calendar_v2_edit_existing = true;
    productivity_calendar_v2_format_input(productivity_calendar_v2_events[source_index]);
}

bool productivity_calendar_v2_commit_input() {
    ProductivityCalendarEventV2 parsed{};
    if (!productivity_calendar_v2_parse_input(productivity_calendar_v2_input, productivity_calendar_v2_input_size, parsed)) {
        serial::line("UI_CALENDAR_V2_INPUT_REJECTED");
        return false;
    }
    if (productivity_calendar_v2_edit_existing) {
        uint8_t source_index = 0U;
        if (!productivity_calendar_v2_selected_index(source_index)) return false;
        const ProductivityCalendarEventV2 previous = productivity_calendar_v2_events[source_index];
        parsed.year = previous.year;
        parsed.month = previous.month;
        parsed.day = previous.day;
        parsed.repeat_day = previous.repeat_day;
        productivity_calendar_v2_events[source_index] = parsed;
        serial::line("UI_CALENDAR_V2_EDIT_OK");
    } else {
        if (productivity_calendar_v2_count >= productivity_calendar_v2_capacity) return false;
        productivity_calendar_v2_events[productivity_calendar_v2_count++] = parsed;
        serial::line("UI_CALENDAR_V2_ADD_OK");
    }
    productivity_calendar_v2_sort();
    productivity_calendar_v2_editing = false;
    productivity_calendar_v2_edit_existing = false;
    productivity_calendar_v2_input_size = 0U;
    productivity_calendar_v2_input[0] = 0;
    productivity_calendar_v2_rebuild_day();
    return productivity_calendar_v2_save();
}

bool productivity_calendar_v2_delete_selected() {
    uint8_t source_index = 0U;
    if (!productivity_calendar_v2_selected_index(source_index)) return false;
    for (uint8_t index = static_cast<uint8_t>(source_index + 1U); index < productivity_calendar_v2_count; ++index)
        productivity_calendar_v2_events[index - 1U] = productivity_calendar_v2_events[index];
    --productivity_calendar_v2_count;
    productivity_calendar_v2_rebuild_day();
    const bool saved = productivity_calendar_v2_save();
    serial::line(saved ? "UI_CALENDAR_V2_DELETE_OK" : "UI_CALENDAR_V2_DELETE_FAILED");
    return saved;
}

void productivity_calendar_v2_draw_event_row(int32_t x, int32_t y, const ProductivityCalendarEventV2& event, bool focused) {
    fill_round_rect(x, y, 164, 38, 6, focused ? current_theme.accent_soft : current_theme.surface_low);
    draw_two_digits(x + 8, y + 9, event.hour, focused ? current_theme.accent : current_theme.text);
    draw_char(x + 22, y + 9, ':', current_theme.text_muted, 1);
    draw_two_digits(x + 30, y + 9, event.minute, focused ? current_theme.accent : current_theme.text);
    char title[19]{};
    string_copy(title, event.title, sizeof(title));
    draw_text(x + 52, y + 9, title, current_theme.text, 1);
    draw_text(x + 8, y + 25, productivity_calendar_v2_repeat_text(event.repeat), event.repeat == ProductivityCalendarRepeat::none ? current_theme.text_muted : current_theme.success, 1);
    draw_uint(x + 80, y + 25, event.duration_minutes, current_theme.text_muted);
    draw_text(x + 104, y + 25, "MIN", current_theme.text_muted, 1);
}

void productivity_calendar_v2_render() {
    static constexpr const char* months[12] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE","JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
    static constexpr const char* weekdays[7] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    draw_window_chrome("ZEN CALENDAR", "TIMED LOCAL EVENTS");
    const int32_t grid_x = window_x + 16, grid_y = window_y + 66;
    fill_round_rect(grid_x, grid_y, 294, 280, 9, current_theme.surface_low);
    draw_text(grid_x + 14, grid_y + 16, months[productivity_calendar_month - 1U], current_theme.text, 2);
    draw_uint(grid_x + 190, grid_y + 16, productivity_calendar_year, current_theme.text_muted);
    for (uint8_t index = 0U; index < 7U; ++index)
        draw_text(grid_x + 8 + static_cast<int32_t>(index * 40U), grid_y + 48, weekdays[index], current_theme.text_muted, 1);
    const uint8_t first = productivity_weekday(productivity_calendar_year, productivity_calendar_month, 1U);
    const uint8_t days = productivity_days_in_month(productivity_calendar_year, productivity_calendar_month);
    for (uint8_t day = 1U; day <= days; ++day) {
        const uint8_t cell = static_cast<uint8_t>(first + day - 1U);
        const int32_t column = cell % 7U;
        const int32_t row = cell / 7U;
        const int32_t x = grid_x + 5 + column * 40;
        const int32_t y = grid_y + 66 + row * 32;
        const bool selected = day == productivity_calendar_day;
        if (selected) fill_round_rect(x, y, 34, 26, 6, current_theme.accent_soft);
        draw_uint(x + 8, y + 10, day, selected ? current_theme.text : current_theme.text_muted);
        const uint8_t event_count = productivity_calendar_v2_events_on(productivity_calendar_year, productivity_calendar_month, day);
        if (event_count) {
            fill_circle_alpha(x + 27, y + 20, 2, event_count > 1U ? current_theme.warning : current_theme.accent, 255);
        }
    }

    const int32_t side_x = window_x + 318;
    fill_round_rect(side_x, grid_y, 178, 280, 9, current_theme.surface_high);
    char selected_date[11]{};
    productivity_calendar_v2_selected_date_text(selected_date);
    draw_text(side_x + 10, grid_y + 14, selected_date, current_theme.accent, 1);
    draw_uint(side_x + 142, grid_y + 14, productivity_calendar_v2_day_count, current_theme.text_muted);
    int32_t event_y = grid_y + 38;
    for (uint8_t row = 0U; row < productivity_calendar_v2_visible_events && row < productivity_calendar_v2_day_count; ++row) {
        const ProductivityCalendarEventV2& event = productivity_calendar_v2_events[productivity_calendar_v2_day_indices[row]];
        productivity_calendar_v2_draw_event_row(side_x + 7, event_y, event, row == productivity_calendar_v2_focus);
        event_y += 44;
    }
    if (!productivity_calendar_v2_day_count) draw_text(side_x + 12, grid_y + 72, "NO EVENTS", current_theme.text_muted, 1);

    if (productivity_calendar_v2_editing) {
        fill_round_rect(grid_x, grid_y + 290, 480, 38, 8, current_theme.accent_soft);
        draw_text(grid_x + 10, grid_y + 304, productivity_calendar_v2_edit_existing ? "EDIT" : "ADD", current_theme.accent, 1);
        char preview[65]{};
        string_copy(preview, productivity_calendar_v2_input_size ? productivity_calendar_v2_input : "TITLE @09:00 +60 !WEEKLY", sizeof(preview));
        draw_text(grid_x + 50, grid_y + 304, preview, productivity_calendar_v2_input_size ? current_theme.text : current_theme.text_muted, 1);
    } else {
        draw_text(grid_x, grid_y + 296, "ARROWS DAY  TAB EVENT  F1 PREV  F2 TODAY  F3 NEXT", current_theme.text_muted, 1);
        draw_text(grid_x, grid_y + 316, "A/F4 ADD  E/F5 EDIT  DELETE REMOVE  ENTER DAILY NOTE", current_theme.text_muted, 1);
    }
}

bool productivity_calendar_v2_handle_input(uint16_t key) {
    if (key == key_escape) {
        productivity_calendar_v2_editing = false;
        productivity_calendar_v2_edit_existing = false;
        productivity_calendar_v2_input_size = 0U;
        productivity_calendar_v2_input[0] = 0;
    } else if (key == '\b' && productivity_calendar_v2_input_size) {
        productivity_calendar_v2_input[--productivity_calendar_v2_input_size] = 0;
    } else if (key == '\n') {
        (void)productivity_calendar_v2_commit_input();
    } else if (key >= 32U && key <= 126U && key != '|' && productivity_calendar_v2_input_size + 1U < sizeof(productivity_calendar_v2_input)) {
        productivity_calendar_v2_input[productivity_calendar_v2_input_size++] = static_cast<char>(key);
        productivity_calendar_v2_input[productivity_calendar_v2_input_size] = 0;
    }
    return true;
}

bool productivity_calendar_v2_handle_key(uint16_t key) {
    if (productivity_calendar_v2_editing) return productivity_calendar_v2_handle_input(key);
    if (key == key_f1) productivity_calendar_change_month(false);
    else if (key == key_f2) productivity_calendar_today();
    else if (key == key_f3) productivity_calendar_change_month(true);
    else if (key == key_f4 || key == 'a' || key == 'A') productivity_calendar_v2_begin_add();
    else if (key == key_f5 || key == 'e' || key == 'E') productivity_calendar_v2_begin_edit();
    else if (key == key_left) productivity_calendar_move_day(-1);
    else if (key == key_right) productivity_calendar_move_day(1);
    else if (key == key_up) productivity_calendar_move_day(-7);
    else if (key == key_down) productivity_calendar_move_day(7);
    else if (key == '\t' && productivity_calendar_v2_day_count)
        productivity_calendar_v2_focus = static_cast<uint8_t>((productivity_calendar_v2_focus + 1U) % productivity_calendar_v2_day_count);
    else if (key == key_delete) (void)productivity_calendar_v2_delete_selected();
    else if (key == '\n' || key == ' ') {
        productivity_open(ProductivityApp::notes);
        (void)productivity_notes_open_daily_for_date(productivity_calendar_year, productivity_calendar_month, productivity_calendar_day);
        return true;
    } else return key != key_escape;
    productivity_calendar_v2_rebuild_day();
    return true;
}
