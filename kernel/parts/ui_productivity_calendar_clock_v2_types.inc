enum class ProductivityCalendarRepeat : uint8_t { none, daily, weekly, monthly };

constexpr uint8_t productivity_calendar_v2_capacity = 16U;
constexpr uint8_t productivity_calendar_v2_title_capacity = 33U;
constexpr uint8_t productivity_calendar_v2_input_capacity = 80U;
constexpr uint8_t productivity_calendar_v2_visible_events = 5U;

struct ProductivityCalendarEventV2 {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint16_t duration_minutes;
    ProductivityCalendarRepeat repeat;
    uint8_t repeat_day;
    char title[productivity_calendar_v2_title_capacity];
};

ProductivityCalendarEventV2 productivity_calendar_v2_events[productivity_calendar_v2_capacity]{};
uint8_t productivity_calendar_v2_count = 0U;
uint8_t productivity_calendar_v2_day_indices[productivity_calendar_v2_capacity]{};
uint8_t productivity_calendar_v2_day_count = 0U;
uint8_t productivity_calendar_v2_focus = 0U;
bool productivity_calendar_v2_editing = false;
bool productivity_calendar_v2_edit_existing = false;
char productivity_calendar_v2_input[productivity_calendar_v2_input_capacity]{};
uint8_t productivity_calendar_v2_input_size = 0U;

constexpr uint8_t productivity_clock_v2_alarm_capacity = 8U;
constexpr uint8_t productivity_clock_v2_alarm_title_capacity = 25U;
constexpr uint8_t productivity_clock_v2_input_capacity = 64U;
constexpr uint8_t productivity_clock_v2_lap_capacity = 8U;
constexpr uint8_t productivity_clock_v2_timer_capacity = 3U;

struct ProductivityClockAlarmV2 {
    uint8_t hour;
    uint8_t minute;
    uint8_t day_mask;
    bool enabled;
    uint32_t last_fired_stamp;
    char title[productivity_clock_v2_alarm_title_capacity];
};

struct ProductivityClockTimerV2 {
    uint32_t seconds;
    uint32_t started_tick;
    bool running;
    bool expired_reported;
};

ProductivityClockAlarmV2 productivity_clock_v2_alarms[productivity_clock_v2_alarm_capacity]{};
uint8_t productivity_clock_v2_alarm_count = 0U;
uint8_t productivity_clock_v2_alarm_focus = 0U;
bool productivity_clock_v2_alarm_editing = false;
bool productivity_clock_v2_alarm_edit_existing = false;
char productivity_clock_v2_input[productivity_clock_v2_input_capacity]{};
uint8_t productivity_clock_v2_input_size = 0U;
uint32_t productivity_clock_v2_laps[productivity_clock_v2_lap_capacity]{};
uint8_t productivity_clock_v2_lap_count = 0U;
ProductivityClockTimerV2 productivity_clock_v2_timers[productivity_clock_v2_timer_capacity]{};
uint8_t productivity_clock_v2_timer_focus = 0U;
int32_t productivity_clock_v2_banner_index = -1;
uint32_t productivity_clock_v2_banner_until = 0U;
