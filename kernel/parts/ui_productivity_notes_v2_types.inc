constexpr uint8_t productivity_notes_v2_history_capacity = 8U;
constexpr uint8_t productivity_notes_v2_visible_rows = 18U;
constexpr uint8_t productivity_notes_v2_visible_columns = 68U;
constexpr uint8_t productivity_notes_v2_tab_width = 4U;

// History stores reversible edit operations, never full 2048-byte documents.
enum class ProductivityNotesV2EditKind : uint8_t { insert, erase };

struct ProductivityNotesV2Edit {
    ProductivityNotesV2EditKind kind;
    uint16_t position;
    uint16_t cursor_before;
    uint16_t cursor_after;
    uint8_t count;
    char bytes[productivity_notes_v2_tab_width];
    bool dirty_before;
    bool dirty_after;
};

static_assert(sizeof(ProductivityNotesV2Edit) <= 16U, "Notes history operation grew beyond its static-memory budget");

ProductivityNotesV2Edit productivity_notes_v2_undo[productivity_notes_v2_history_capacity]{};
ProductivityNotesV2Edit productivity_notes_v2_redo[productivity_notes_v2_history_capacity]{};
uint8_t productivity_notes_v2_undo_count = 0U;
uint8_t productivity_notes_v2_redo_count = 0U;
uint16_t productivity_notes_v2_cursor = 0U;
uint16_t productivity_notes_v2_preferred_column = 0U;
uint16_t productivity_notes_v2_scroll_line = 0U;
uint16_t productivity_notes_v2_scroll_column = 0U;
bool productivity_notes_v2_preferred_valid = false;
bool productivity_notes_v2_bound = false;
char productivity_notes_v2_bound_path[48]{};
