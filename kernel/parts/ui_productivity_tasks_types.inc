enum class ProductivityTaskFilter : uint8_t { open, all, done };

constexpr uint32_t productivity_task_capacity = 24U;
constexpr uint32_t productivity_task_visible_rows = 8U;
constexpr uint32_t productivity_task_text_capacity = 65U;

struct ProductivityTask {
    char source_path[48];
    char text[productivity_task_text_capacity];
    uint16_t state_offset;
    uint16_t line_number;
    uint16_t due_year;
    uint8_t due_month;
    uint8_t due_day;
    uint8_t priority;
    bool done;
    bool waiting;
};

ProductivityTask productivity_tasks[productivity_task_capacity]{};
uint8_t productivity_task_visible[productivity_task_capacity]{};
uint8_t productivity_task_count = 0U;
uint8_t productivity_task_visible_count = 0U;
uint8_t productivity_task_focus = 0U;
uint8_t productivity_task_scroll = 0U;
ProductivityTaskFilter productivity_task_filter = ProductivityTaskFilter::open;
bool productivity_tasks_open = false;
bool productivity_task_adding = false;
char productivity_task_input[48]{};
uint8_t productivity_task_input_size = 0U;

bool productivity_task_tag_boundary(char value) {
    return value == ' ' || value == '\t';
}

bool productivity_task_line_has_token_ci(const char* data, uint32_t start, uint32_t end, const char* token) {
    if (!data || !token) return false;
    const uint32_t token_size = static_cast<uint32_t>(string_length(token));
    if (!token_size || end < start || token_size > end - start) return false;
    for (uint32_t position = start; position + token_size <= end; ++position) {
        if (position != start && !productivity_task_tag_boundary(data[position - 1U])) continue;
        if (position + token_size != end && !productivity_task_tag_boundary(data[position + token_size])) continue;
        bool match = true;
        for (uint32_t index = 0U; index < token_size; ++index) {
            if (!productivity_ascii_equal_ci(data[position + index], token[index])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool productivity_task_parse_due(const char* data, uint32_t start, uint32_t end,
                                 uint16_t& year, uint8_t& month, uint8_t& day) {
    year = 0U;
    month = 0U;
    day = 0U;
    constexpr uint32_t token_size = 13U;
    if (!data || end < start || end - start < token_size) return false;
    for (uint32_t position = start; position + token_size <= end; ++position) {
        if ((position != start && !productivity_task_tag_boundary(data[position - 1U])) ||
            (position + token_size != end && !productivity_task_tag_boundary(data[position + token_size])) ||
            data[position] != '#' || !productivity_ascii_equal_ci(data[position + 1U], 'D') ||
            data[position + 2U] != '-' || data[position + 7U] != '-' || data[position + 10U] != '-') continue;
        uint32_t parsed_year = 0U, parsed_month = 0U, parsed_day = 0U;
        if (!productivity_parse_digits(data + position + 3U, 4U, parsed_year) ||
            !productivity_parse_digits(data + position + 8U, 2U, parsed_month) ||
            !productivity_parse_digits(data + position + 11U, 2U, parsed_day)) continue;
        if (parsed_year < 2000U || parsed_year > 2099U || parsed_month < 1U || parsed_month > 12U ||
            parsed_day < 1U || parsed_day > productivity_days_in_month(static_cast<uint16_t>(parsed_year), static_cast<uint8_t>(parsed_month))) continue;
        year = static_cast<uint16_t>(parsed_year);
        month = static_cast<uint8_t>(parsed_month);
        day = static_cast<uint8_t>(parsed_day);
        return true;
    }
    return false;
}

int32_t productivity_task_compare_text(const char* left, const char* right) {
    uint32_t index = 0U;
    while (left[index] && right[index]) {
        char left_value = left[index];
        char right_value = right[index];
        if (left_value >= 'A' && left_value <= 'Z') left_value = static_cast<char>(left_value + ('a' - 'A'));
        if (right_value >= 'A' && right_value <= 'Z') right_value = static_cast<char>(right_value + ('a' - 'A'));
        if (left_value < right_value) return -1;
        if (left_value > right_value) return 1;
        ++index;
    }
    if (!left[index] && !right[index]) return 0;
    return left[index] ? 1 : -1;
}

bool productivity_task_due_before(const ProductivityTask& left, const ProductivityTask& right) {
    const bool left_has_due = left.due_year != 0U;
    const bool right_has_due = right.due_year != 0U;
    if (left_has_due != right_has_due) return left_has_due;
    if (left_has_due) {
        if (left.due_year != right.due_year) return left.due_year < right.due_year;
        if (left.due_month != right.due_month) return left.due_month < right.due_month;
        if (left.due_day != right.due_day) return left.due_day < right.due_day;
    }
    return false;
}

bool productivity_task_before(const ProductivityTask& left, const ProductivityTask& right) {
    if (left.done != right.done) return !left.done;
    if (left.waiting != right.waiting) return !left.waiting;
    if (left.priority != right.priority) return left.priority < right.priority;
    if (productivity_task_due_before(left, right)) return true;
    if (productivity_task_due_before(right, left)) return false;
    const int32_t source_order = productivity_task_compare_text(left.source_path, right.source_path);
    if (source_order != 0) return source_order < 0;
    return left.line_number < right.line_number;
}
