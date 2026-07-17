constexpr uint32_t width = 800U, height = 600U, bytes_per_pixel = 4U;
constexpr uint32_t framebuffer_bytes = width * height * bytes_per_pixel;
constexpr uint16_t vbe_index_port = 0x01CE, vbe_data_port = 0x01CF;
constexpr uint16_t vbe_id = 0, vbe_xres = 1, vbe_yres = 2, vbe_bpp = 3, vbe_enable = 4, vbe_virtual_width = 6;
constexpr uint16_t vbe_enabled = 0x01, vbe_linear_framebuffer = 0x40;
constexpr uint32_t qemu_vga_id = 0x11111234U;
constexpr int32_t window_width = 510, window_height = 390, titlebar_height = 40;
constexpr uint32_t desktop_canvas = 0x0B1020U, desktop_surface = 0x141A2BU, desktop_accent = 0x8B7CF6U;
constexpr uint8_t desktop_radius = 14U, desktop_opacity = 232U;
constexpr bool desktop_animations = true;
constexpr const char* desktop_theme = "zen-dark";
constexpr const char* terminal_cursor = "beam";

enum class View : uint8_t { terminal, files, settings, launcher };

struct Glyph { char value; uint8_t rows[7]; };
static constexpr Glyph glyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
    {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}}, {'I',{31,4,4,4,4,4,31}},
    {'J',{7,2,2,2,18,18,12}}, {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}},
    {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
    {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,1,14}}, {'-',{0,0,0,31,0,0,0}}, {'.',{0,0,0,0,0,12,12}},
    {':',{0,12,12,0,12,12,0}}, {'/',{1,2,2,4,8,8,16}}, {'\\',{16,8,8,4,2,2,1}},
    {'_',{0,0,0,0,0,0,31}}, {'+',{0,4,4,31,4,4,0}}, {'=',{0,31,0,31,0,0,0}},
    {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}}, {'(',{2,4,8,8,8,4,2}},
    {')',{8,4,2,2,2,4,8}}, {'<',{2,4,8,16,8,4,2}}, {'>',{8,4,2,1,2,4,8}},
    {'?',{14,17,1,2,4,0,4}}, {'!',{4,4,4,4,4,0,4}}, {'@',{14,17,23,21,23,16,14}},
    {'#',{10,31,10,10,31,10,0}}, {'%',{17,2,4,8,17,0,0}}, {'*',{0,21,14,31,14,21,0}},
    {'|',{4,4,4,4,4,4,4}}, {',',{0,0,0,0,0,4,8}}, {';',{0,4,4,0,4,4,8}},
    {'\'',{4,4,8,0,0,0,0}}, {'"',{10,10,0,0,0,0,0}}, {' ',{0,0,0,0,0,0,0}}
};

struct Theme {
    uint32_t canvas_top, canvas_bottom, glow_primary, glow_secondary;
    uint32_t surface, surface_high, surface_low, border;
    uint32_t text, text_muted, accent, accent_soft;
    uint32_t success, warning, danger, terminal;
};

volatile uint32_t* framebuffer = nullptr;
uint32_t* backbuffer = nullptr;
uintptr_t physical_framebuffer = 0;
bool active = false, first_mouse_packet = true, dragging = false, drag_reported = false;
bool terminal_sync_started = false, runtime_preferences_loaded = false;
int32_t mouse_x = static_cast<int32_t>(width / 2U), mouse_y = static_cast<int32_t>(height / 2U);
int32_t window_x = 150, window_y = 88, drag_offset_x = 0, drag_offset_y = 0;
uint8_t mouse_buttons = 0, runtime_theme = 0;
View active_view = View::terminal;
Theme current_theme{};

inline void io_out16(uint16_t port, uint16_t value) { asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
inline uint16_t io_in16(uint16_t port) { uint16_t value; asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }
inline void io_out32(uint16_t port, uint32_t value) { asm volatile("outl %0, %1" : : "a"(value), "Nd"(port)); }
inline uint32_t io_in32(uint16_t port) { uint32_t value; asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port)); return value; }

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    const uint32_t address = 0x80000000U | (static_cast<uint32_t>(bus) << 16U) |
        (static_cast<uint32_t>(device) << 11U) | (static_cast<uint32_t>(function) << 8U) | (offset & 0xFCU);
    io_out32(0x0CF8, address);
    return io_in32(0x0CFC);
}

uintptr_t find_qemu_framebuffer() {
    for (uint32_t device = 0; device < 32U; ++device) {
        for (uint32_t function = 0; function < 8U; ++function) {
            const uint32_t identity = pci_read(0, static_cast<uint8_t>(device), static_cast<uint8_t>(function), 0);
            if (identity == 0xFFFFFFFFU) { if (function == 0) break; continue; }
            if (identity != qemu_vga_id) continue;
            const uint32_t bar = pci_read(0, static_cast<uint8_t>(device), static_cast<uint8_t>(function), 0x10);
            if (!bar || bar == 0xFFFFFFFFU || (bar & 1U)) return 0;
            return static_cast<uintptr_t>(bar & 0xFFFFFFF0U);
        }
    }
    return 0;
}

void vbe_write(uint16_t index, uint16_t value) { io_out16(vbe_index_port, index); io_out16(vbe_data_port, value); }
uint16_t vbe_read(uint16_t index) { io_out16(vbe_index_port, index); return io_in16(vbe_data_port); }

constexpr uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return (static_cast<uint32_t>(red) << 16U) | (static_cast<uint32_t>(green) << 8U) | blue;
}
uint32_t blend(uint32_t source, uint32_t destination, uint8_t alpha) {
    const uint32_t inverse = 255U - alpha;
    const uint32_t red = ((((source >> 16U) & 0xFFU) * alpha) + (((destination >> 16U) & 0xFFU) * inverse) + 127U) / 255U;
    const uint32_t green = ((((source >> 8U) & 0xFFU) * alpha) + (((destination >> 8U) & 0xFFU) * inverse) + 127U) / 255U;
    const uint32_t blue = (((source & 0xFFU) * alpha) + ((destination & 0xFFU) * inverse) + 127U) / 255U;
    return (red << 16U) | (green << 8U) | blue;
}
uint32_t mix(uint32_t first, uint32_t second, uint32_t numerator, uint32_t denominator) {
    if (!denominator) return first;
    const uint32_t inverse = denominator - numerator;
    const uint32_t red = ((((first >> 16U) & 0xFFU) * inverse) + (((second >> 16U) & 0xFFU) * numerator)) / denominator;
    const uint32_t green = ((((first >> 8U) & 0xFFU) * inverse) + (((second >> 8U) & 0xFFU) * numerator)) / denominator;
    const uint32_t blue = (((first & 0xFFU) * inverse) + ((second & 0xFFU) * numerator)) / denominator;
    return (red << 16U) | (green << 8U) | blue;
}

bool clip_rect(int32_t& x, int32_t& y, int32_t& rect_width, int32_t& rect_height) {
    if (rect_width <= 0 || rect_height <= 0 || x >= static_cast<int32_t>(width) || y >= static_cast<int32_t>(height) || x + rect_width <= 0 || y + rect_height <= 0) return false;
    if (x < 0) { rect_width += x; x = 0; }
    if (y < 0) { rect_height += y; y = 0; }
    if (x + rect_width > static_cast<int32_t>(width)) rect_width = static_cast<int32_t>(width) - x;
    if (y + rect_height > static_cast<int32_t>(height)) rect_height = static_cast<int32_t>(height) - y;
    return rect_width > 0 && rect_height > 0;
}

void fill_rect(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, uint32_t color) {
    if (!backbuffer || !clip_rect(x, y, rect_width, rect_height)) return;
    for (int32_t row = 0; row < rect_height; ++row) {
        uint32_t* output = backbuffer + static_cast<uint32_t>(y + row) * width + static_cast<uint32_t>(x);
        for (int32_t column = 0; column < rect_width; ++column) output[column] = color;
    }
}

void fill_rect_alpha(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, uint32_t color, uint8_t alpha) {
    if (!backbuffer || !clip_rect(x, y, rect_width, rect_height)) return;
    for (int32_t row = 0; row < rect_height; ++row) {
        uint32_t* output = backbuffer + static_cast<uint32_t>(y + row) * width + static_cast<uint32_t>(x);
        for (int32_t column = 0; column < rect_width; ++column) output[column] = blend(color, output[column], alpha);
    }
}

void vertical_gradient(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, uint32_t top, uint32_t bottom) {
    if (rect_height <= 0) return;
    for (int32_t row = 0; row < rect_height; ++row) fill_rect(x, y + row, rect_width, 1, mix(top, bottom, static_cast<uint32_t>(row), static_cast<uint32_t>(rect_height)));
}

void fill_circle_alpha(int32_t center_x, int32_t center_y, int32_t radius, uint32_t color, uint8_t alpha) {
    if (radius <= 0) return;
    const int32_t radius_squared = radius * radius;
    for (int32_t dy = -radius; dy <= radius; ++dy) {
        int32_t span = radius;
        while (span > 0 && span * span + dy * dy > radius_squared) --span;
        fill_rect_alpha(center_x - span, center_y + dy, span * 2 + 1, 1, color, alpha);
    }
}

void fill_round_rect(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t radius, uint32_t color) {
    if (rect_width <= 0 || rect_height <= 0) return;
    const int32_t maximum = (rect_width < rect_height ? rect_width : rect_height) / 2;
    if (radius < 0) radius = 0;
    if (radius > maximum) radius = maximum;
    if (!radius) { fill_rect(x, y, rect_width, rect_height, color); return; }
    fill_rect(x + radius, y, rect_width - radius * 2, rect_height, color);
    fill_rect(x, y + radius, radius, rect_height - radius * 2, color);
    fill_rect(x + rect_width - radius, y + radius, radius, rect_height - radius * 2, color);
    const int32_t radius_squared = radius * radius;
    for (int32_t row = 0; row < radius; ++row) {
        const int32_t dy = radius - row - 1;
        int32_t span = radius;
        while (span > 0 && span * span + dy * dy > radius_squared) --span;
        const int32_t line_x = x + radius - span;
        const int32_t line_width = rect_width - (radius - span) * 2;
        fill_rect(line_x, y + row, line_width, 1, color);
        fill_rect(line_x, y + rect_height - row - 1, line_width, 1, color);
    }
}

void fill_round_rect_alpha(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t radius, uint32_t color, uint8_t alpha) {
    if (rect_width <= 0 || rect_height <= 0) return;
    const int32_t maximum = (rect_width < rect_height ? rect_width : rect_height) / 2;
    if (radius < 0) radius = 0;
    if (radius > maximum) radius = maximum;
    if (!radius) { fill_rect_alpha(x, y, rect_width, rect_height, color, alpha); return; }
    fill_rect_alpha(x + radius, y, rect_width - radius * 2, rect_height, color, alpha);
    fill_rect_alpha(x, y + radius, radius, rect_height - radius * 2, color, alpha);
    fill_rect_alpha(x + rect_width - radius, y + radius, radius, rect_height - radius * 2, color, alpha);
    const int32_t radius_squared = radius * radius;
    for (int32_t row = 0; row < radius; ++row) {
        const int32_t dy = radius - row - 1;
        int32_t span = radius;
        while (span > 0 && span * span + dy * dy > radius_squared) --span;
        const int32_t line_x = x + radius - span;
        const int32_t line_width = rect_width - (radius - span) * 2;
        fill_rect_alpha(line_x, y + row, line_width, 1, color, alpha);
        fill_rect_alpha(line_x, y + rect_height - row - 1, line_width, 1, color, alpha);
    }
}

void draw_round_border(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t radius, uint32_t color) {
    fill_round_rect(x, y, rect_width, rect_height, radius, color);
    fill_round_rect(x + 1, y + 1, rect_width - 2, rect_height - 2, radius > 1 ? radius - 1 : 0, current_theme.surface);
}

void draw_shadow(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t radius) {
    fill_round_rect_alpha(x + 10, y + 14, rect_width, rect_height, radius + 4, rgb(0, 0, 0), 52);
    fill_round_rect_alpha(x + 5, y + 7, rect_width, rect_height, radius + 2, rgb(0, 0, 0), 44);
}

const Glyph* glyph_for(char value) {
    if (value >= 'a' && value <= 'z') value = static_cast<char>(value - ('a' - 'A'));
    for (uint32_t index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); ++index) if (glyphs[index].value == value) return &glyphs[index];
    for (uint32_t index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); ++index) if (glyphs[index].value == '?') return &glyphs[index];
    return nullptr;
}

void draw_char(int32_t x, int32_t y, char value, uint32_t color, uint32_t scale = 1U) {
    const Glyph* glyph = glyph_for(value);
    if (!glyph || !scale) return;
    for (uint32_t row = 0; row < 7U; ++row) {
        for (uint32_t column = 0; column < 5U; ++column) {
            if (glyph->rows[row] & (1U << (4U - column))) fill_rect(x + static_cast<int32_t>(column * scale), y + static_cast<int32_t>(row * scale), static_cast<int32_t>(scale), static_cast<int32_t>(scale), color);
        }
    }
}

void draw_text(int32_t x, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) {
    while (text && *text) { draw_char(x, y, *text++, color, scale); x += static_cast<int32_t>(6U * scale); }
}

void draw_uint(int32_t x, int32_t y, uint32_t value, uint32_t color) {
    char buffer[11]; uint32_t count = 0;
    do { buffer[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(buffer));
    while (count) { draw_char(x, y, buffer[--count], color, 1); x += 6; }
}

Theme theme_for(uint8_t id) {
    if (id == 1U) return Theme{
        rgb(15,17,22), rgb(27,30,38), rgb(94,104,128), rgb(45,54,70),
        rgb(27,30,38), rgb(39,44,55), rgb(20,22,28), rgb(72,79,94),
        rgb(239,242,248), rgb(154,163,181), rgb(126,142,177), rgb(55,65,84),
        rgb(111,211,160), rgb(235,190,91), rgb(235,105,126), rgb(12,14,19)};
    if (id == 2U) return Theme{
        rgb(25,13,17), rgb(51,27,26), rgb(244,166,73), rgb(166,77,52),
        rgb(44,27,29), rgb(61,37,38), rgb(29,19,22), rgb(103,61,58),
        rgb(255,239,218), rgb(205,166,141), rgb(245,174,82), rgb(90,53,42),
        rgb(132,213,154), rgb(246,199,91), rgb(240,104,113), rgb(20,13,16)};
    return Theme{
        desktop_canvas, rgb(20,17,43), rgb(139,124,246), rgb(72,170,219),
        desktop_surface, rgb(35,39,67), rgb(13,15,29), rgb(73,77,110),
        rgb(239,241,255), rgb(164,169,199), desktop_accent, rgb(57,52,104),
        rgb(113,217,168), rgb(242,198,109), rgb(239,125,146), rgb(9,10,20)};
}

void apply_runtime_theme(uint8_t id) {
    runtime_theme = id > 2U ? 0U : id;
    current_theme = theme_for(runtime_theme);
}

