enum class ProductivityUtilityApp : uint8_t { none, calculator, reminders };
enum class ProductivityCalculatorMode : uint8_t { standard, programmer, date, units };
enum class ProductivityCalculatorError : uint8_t { none, syntax, overflow, divide_by_zero, shift_range };
enum class ProductivityUnitGroup : uint8_t { length, temperature, storage };
enum class ProductivityReminderFilter : uint8_t { today, overdue, week, scheduled, done };
enum class ProductivityReminderRepeat : uint8_t { none, daily, weekly, monthly };
enum class ProductivityAgendaKind : uint8_t { reminder, task, event };

constexpr uint8_t productivity_calculator_history_capacity = 8U;
constexpr uint8_t productivity_calculator_input_capacity = 49U;
constexpr uint8_t productivity_reminder_capacity = 24U;
constexpr uint8_t productivity_reminder_input_capacity = 65U;
constexpr uint8_t productivity_agenda_capacity = 48U;
constexpr uint8_t productivity_agenda_visible_rows = 8U;
constexpr int32_t productivity_calc_int_max = 2147483647;
constexpr int32_t productivity_calc_int_min = (-2147483647 - 1);

struct ProductivityCalculatorDate {
    uint16_t year;
    uint8_t month;
    uint8_t day;
};

struct ProductivityReminder {
    char title[41];
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    bool done;
    bool notified;
    ProductivityReminderRepeat repeat;
    uint8_t repeat_day;
};

struct ProductivityAgendaItem {
    ProductivityAgendaKind kind;
    uint8_t source_index;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    bool done;
    bool overdue;
    char title[49];
};

ProductivityUtilityApp productivity_utility_app = ProductivityUtilityApp::none;
ProductivityCalculatorMode productivity_calculator_mode = ProductivityCalculatorMode::standard;
ProductivityCalculatorError productivity_calculator_error = ProductivityCalculatorError::none;
char productivity_calculator_input[productivity_calculator_input_capacity]{};
uint8_t productivity_calculator_input_size = 0U;
int32_t productivity_calculator_result = 0;
bool productivity_calculator_has_result = false;
int32_t productivity_calculator_memory = 0;
char productivity_calculator_history_expression[productivity_calculator_history_capacity][productivity_calculator_input_capacity]{};
int32_t productivity_calculator_history_result[productivity_calculator_history_capacity]{};
uint8_t productivity_calculator_history_count = 0U;
ProductivityCalculatorDate productivity_calculator_date_a{2026U, 1U, 1U};
ProductivityCalculatorDate productivity_calculator_date_b{2026U, 1U, 2U};
bool productivity_calculator_date_select_b = false;
ProductivityUnitGroup productivity_unit_group = ProductivityUnitGroup::length;
uint8_t productivity_unit_from = 0U;
uint8_t productivity_unit_to = 2U;

ProductivityReminder productivity_reminders[productivity_reminder_capacity]{};
uint8_t productivity_reminder_count = 0U;
ProductivityReminderFilter productivity_reminder_filter = ProductivityReminderFilter::today;
ProductivityAgendaItem productivity_agenda[productivity_agenda_capacity]{};
uint8_t productivity_agenda_count = 0U;
uint8_t productivity_agenda_focus = 0U;
uint8_t productivity_agenda_scroll = 0U;
bool productivity_reminder_adding = false;
char productivity_reminder_input[productivity_reminder_input_capacity]{};
uint8_t productivity_reminder_input_size = 0U;
ProductivityReminderRepeat productivity_reminder_add_repeat = ProductivityReminderRepeat::none;
bool productivity_utilities_storage_ready = false;
bool productivity_reminder_banner_active = false;
char productivity_reminder_banner_title[41]{};
ProductivityReminderRepeat productivity_reminder_banner_repeat = ProductivityReminderRepeat::none;
uint32_t productivity_reminder_banner_started = 0U;
