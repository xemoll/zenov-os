#include "ui_productivity_calendar_clock_v2_types.inc"
#include "ui_productivity_calendar_clock_v2_common.inc"

#define productivity_append_text productivity_calendar_clock_v2_append_text
#define productivity_append_uint productivity_calendar_clock_v2_append_uint

#define productivity_calendar_v2_save productivity_calendar_v2_save_impl
#include "ui_productivity_calendar_v2_storage.inc"
#undef productivity_calendar_v2_save

bool productivity_calendar_v2_save() {
    const bool saved = productivity_calendar_v2_save_impl();
    if (saved) serial::line("UI_CALENDAR_SAVE_OK");
    return saved;
}

#include "ui_productivity_calendar_v2_ui.inc"

#define productivity_clock_v2_add_minute productivity_clock_v2_add_minute_impl
#define productivity_clock_v2_toggle_timer productivity_clock_v2_toggle_timer_impl
#include "ui_productivity_clock_v2.inc"
#undef productivity_clock_v2_toggle_timer
#undef productivity_clock_v2_add_minute

void productivity_clock_v2_add_minute() {
    productivity_clock_v2_add_minute_impl();
    serial::line("UI_CLOCK_TIMER_SET_OK");
}

void productivity_clock_v2_toggle_timer() {
    productivity_clock_v2_toggle_timer_impl();
    if (productivity_clock_v2_timers[productivity_clock_v2_timer_focus].running)
        serial::line("UI_CLOCK_TIMER_RUNNING");
}

#include "ui_productivity_clock_v2_ui.inc"
#undef productivity_append_uint
#undef productivity_append_text

#include "ui_productivity_calendar_clock_v2_runtime.inc"
