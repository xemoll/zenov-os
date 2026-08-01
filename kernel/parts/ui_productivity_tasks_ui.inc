void productivity_tasks_render() {
    draw_window_chrome("ZEN TASKS", "MARKDOWN TASK INDEX");
    const int32_t x = window_x + 16, y = window_y + 62;
    fill_round_rect(x, y, 480, 38, 8, current_theme.surface_low);
    const char* filter = productivity_task_filter == ProductivityTaskFilter::open ? "OPEN" :
        (productivity_task_filter == ProductivityTaskFilter::done ? "DONE" : "ALL");
    draw_text(x + 14, y + 15, "FILTER", current_theme.text_muted, 1);
    draw_text(x + 60, y + 15, filter, current_theme.accent, 1);
    draw_text_right(x + 462, y + 15, "F1 OPEN  F2 ALL  F3 DONE  F4 ADD", current_theme.text_muted, 1);

    fill_round_rect(x, y + 48, 342, 246, 8, current_theme.surface_low);
    for (uint8_t row = 0U; row < productivity_task_visible_rows; ++row) {
        const uint8_t visible_index = static_cast<uint8_t>(productivity_task_scroll + row);
        if (visible_index >= productivity_task_visible_count) break;
        const ProductivityTask& task = productivity_tasks[productivity_task_visible[visible_index]];
        const int32_t row_y = y + 66 + static_cast<int32_t>(row * 27U);
        if (visible_index == productivity_task_focus) fill_round_rect(x + 8, row_y - 9, 326, 23, 6, current_theme.accent_soft);
        draw_text(x + 16, row_y, task.done ? "[x]" : "[ ]", task.done ? current_theme.success : current_theme.text, 1);
        char label[43]{};
        string_copy(label, task.text, sizeof(label));
        draw_text(x + 48, row_y, label, task.done ? current_theme.text_muted : current_theme.text, 1);
        if (task.priority < 4U) {
            draw_char(x + 302, row_y, 'P', current_theme.accent, 1);
            draw_uint(x + 310, row_y, task.priority, current_theme.accent);
        }
        if (productivity_task_is_overdue(task)) draw_char(x + 326, row_y, '!', current_theme.warning, 1);
    }
    if (!productivity_task_visible_count) draw_text(x + 18, y + 112, "NO TASKS IN THIS FILTER", current_theme.text_muted, 1);

    fill_round_rect(x + 352, y + 48, 128, 246, 8, current_theme.surface_high);
    draw_text(x + 364, y + 64, "DETAILS", current_theme.text_muted, 1);
    if (productivity_task_visible_count) {
        const ProductivityTask& task = productivity_tasks[productivity_task_visible[productivity_task_focus]];
        draw_text(x + 364, y + 92, task.done ? "COMPLETED" : "ACTIVE", task.done ? current_theme.success : current_theme.accent, 1);
        draw_text(x + 364, y + 118, task.waiting ? "WAITING" : "ACTIONABLE", task.waiting ? current_theme.warning : current_theme.text, 1);
        draw_text(x + 364, y + 150, "SOURCE", current_theme.text_muted, 1);
        const char* name = task.source_path;
        for (const char* cursor = task.source_path; *cursor; ++cursor) if (*cursor == '/') name = cursor + 1;
        char source[18]{};
        string_copy(source, name, sizeof(source));
        draw_text(x + 364, y + 170, source, current_theme.text, 1);
        if (task.due_year) {
            char due[11]{};
            productivity_date_text(due, task.due_year, task.due_month, task.due_day);
            draw_text(x + 364, y + 202, "DUE", current_theme.text_muted, 1);
            draw_text(x + 364, y + 222, due, productivity_task_is_overdue(task) ? current_theme.warning : current_theme.text, 1);
        }
    }
    if (productivity_task_adding) {
        fill_round_rect(x, y + 306, 480, 34, 8, current_theme.accent_soft);
        draw_text(x + 12, y + 319, productivity_task_input_size ? productivity_task_input : "TYPE TASK TITLE", productivity_task_input_size ? current_theme.text : current_theme.text_muted, 1);
    } else {
        draw_text(x, y + 316, "ENTER TOGGLE   O OPEN SOURCE   R RESCAN   ESC CLOSE", current_theme.text_muted, 1);
    }
}

void productivity_tasks_open_app() {
    if (productivity_note_dirty) (void)productivity_notes_save();
    productivity_tasks_open = true;
    productivity_overlay_open = true;
    productivity_note_searching = false;
    productivity_event_editing = false;
    productivity_task_adding = false;
    productivity_tasks_scan();
    serial::line("UI_TASKS_OPEN_APP_OK");
}

void productivity_tasks_close_app() {
    productivity_tasks_open = false;
    productivity_task_adding = false;
    productivity_task_input_size = 0U;
    productivity_task_input[0] = 0;
    productivity_close();
}

bool productivity_tasks_handle_key(uint16_t key) {
    if (productivity_task_adding) {
        if (key == key_escape) {
            productivity_task_adding = false;
            productivity_task_input_size = 0U;
            productivity_task_input[0] = 0;
        } else if (key == '\b' && productivity_task_input_size) {
            productivity_task_input[--productivity_task_input_size] = 0;
        } else if (key == '\n') {
            (void)productivity_tasks_append_quick();
        } else if (key >= 32U && key <= 126U && productivity_task_input_size + 1U < sizeof(productivity_task_input)) {
            productivity_task_input[productivity_task_input_size++] = static_cast<char>(key);
            productivity_task_input[productivity_task_input_size] = 0;
        }
        return true;
    }
    if (key == key_f1) productivity_task_filter = ProductivityTaskFilter::open;
    else if (key == key_f2) productivity_task_filter = ProductivityTaskFilter::all;
    else if (key == key_f3) productivity_task_filter = ProductivityTaskFilter::done;
    else if (key == key_f4) {
        productivity_task_adding = true;
        productivity_task_input_size = 0U;
        productivity_task_input[0] = 0;
        return true;
    } else if (key == key_up && productivity_task_visible_count) {
        productivity_task_focus = productivity_task_focus == 0U ? static_cast<uint8_t>(productivity_task_visible_count - 1U) : static_cast<uint8_t>(productivity_task_focus - 1U);
    } else if ((key == key_down || key == key_right || key == '\t') && productivity_task_visible_count) {
        productivity_task_focus = static_cast<uint8_t>((productivity_task_focus + 1U) % productivity_task_visible_count);
    } else if ((key == '\n' || key == ' ') && productivity_task_visible_count) {
        (void)productivity_tasks_toggle_selected();
        return true;
    } else if ((key == 'o' || key == 'O') && productivity_task_visible_count) {
        char source_path[48]{};
        string_copy(source_path, productivity_tasks[productivity_task_visible[productivity_task_focus]].source_path, sizeof(source_path));
        productivity_tasks_open = false;
        productivity_open(ProductivityApp::notes);
        (void)productivity_notes_open_path(source_path);
        return true;
    } else if (key == 'r' || key == 'R') {
        productivity_tasks_scan();
        return true;
    } else if (key == key_escape) {
        productivity_tasks_close_app();
        return true;
    } else return true;
    productivity_tasks_rebuild_visible();
    return true;
}
