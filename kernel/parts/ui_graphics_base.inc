constexpr uint32_t width = 800U, height = 600U, bytes_per_pixel = 4U;
constexpr uint32_t backbuffer_bytes = width * height * bytes_per_pixel;
constexpr uint16_t vbe_index_port = 0x01CE, vbe_data_port = 0x01CF;
constexpr uint16_t vbe_id = 0, vbe_xres = 1, vbe_yres = 2, vbe_bpp = 3, vbe_enable = 4, vbe_virtual_width = 6;
constexpr uint16_t vbe_enabled = 0x01, vbe_linear_framebuffer = 0x40;
constexpr uint32_t qemu_vga_id = 0x11111234U;
constexpr int32_t ui_grid = 8;
constexpr int32_t window_width = 512, window_height = 400, titlebar_height = 40;
constexpr int32_t topbar_x = 24, topbar_y = 16, topbar_width = 752;
constexpr uint32_t desktop_canvas = 0x0D111BU, desktop_surface = 0x171C28U, desktop_accent = 0x7D8CFFU;
constexpr uint8_t desktop_radius = 12U, desktop_opacity = 240U;
constexpr bool desktop_animations = true;
constexpr const char* desktop_theme = "midnight";
constexpr const char* terminal_cursor = "beam";

struct DisplayMode { uint16_t width, height; };
constexpr DisplayMode display_modes[] = {
    {640, 480}, {720, 480}, {800, 600}, {960, 540}, {960, 600},
    {1024, 576}, {1024, 600}, {1024, 768}, {1152, 648}, {1152, 720},
    {1152, 864}, {1280, 720}, {1280, 768}, {1280, 800}, {1280, 960},
    {1280, 1024}, {1360, 768}, {1368, 768}, {1440, 900}, {1536, 864},
    {1600, 900}, {1600, 1200}
};
constexpr uint8_t display_mode_count = static_cast<uint8_t>(sizeof(display_modes) / sizeof(display_modes[0]));
constexpr uint8_t default_display_mode = 7U;
constexpr uint32_t maximum_framebuffer_bytes = 1600U * 1200U * bytes_per_pixel;
uint8_t requested_display_mode = default_display_mode;
uint8_t active_display_mode = default_display_mode;
uint32_t display_width = display_modes[default_display_mode].width;
uint32_t display_height = display_modes[default_display_mode].height;
int32_t viewport_x = 0, viewport_y = 0, viewport_width = static_cast<int32_t>(display_width), viewport_height = static_cast<int32_t>(display_height);

bool display_mode_valid(uint8_t mode) { return mode < display_mode_count; }
uint32_t display_framebuffer_bytes() { return display_width * display_height * bytes_per_pixel; }

void configure_viewport() {
    const uint32_t wide = display_width * height;
    const uint32_t tall = display_height * width;
    if (wide >= tall) {
        viewport_height = static_cast<int32_t>(display_height);
        viewport_width = static_cast<int32_t>((display_height * width) / height);
        viewport_x = (static_cast<int32_t>(display_width) - viewport_width) / 2;
        viewport_y = 0;
    } else {
        viewport_width = static_cast<int32_t>(display_width);
        viewport_height = static_cast<int32_t>((display_width * height) / width);
        viewport_x = 0;
        viewport_y = (static_cast<int32_t>(display_height) - viewport_height) / 2;
    }
}

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

#include "ui_font.inc"

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
int32_t mouse_screen_x = static_cast<int32_t>(display_width / 2U), mouse_screen_y = static_cast<int32_t>(display_height / 2U);
int32_t window_x = 144, window_y = 76, drag_offset_x = 0, drag_offset_y = 0;
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
    if (numerator > denominator) numerator = denominator;
    const uint32_t inverse = denominator - numerator;
    const uint32_t red = ((((first >> 16U) & 0xFFU) * inverse) + (((second >> 16U) & 0xFFU) * numerator)) / denominator;
    const uint32_t green = ((((first >> 8U) & 0xFFU) * inverse) + (((second >> 8U) & 0xFFU) * numerator)) / denominator;
    const uint32_t blue = (((first & 0xFFU) * inverse) + ((second & 0xFFU) * numerator)) / denominator;
    return (red << 16U) | (green << 8U) | blue;
}

bool clip_rect_to_bounds(int32_t& x, int32_t& y, int32_t& rect_width, int32_t& rect_height,
                         int32_t bounds_width, int32_t bounds_height) {
    if (rect_width <= 0 || rect_height <= 0 || bounds_width <= 0 || bounds_height <= 0) return false;
    int64_t left = x;
    int64_t top = y;
    int64_t right = left + static_cast<int64_t>(rect_width);
    int64_t bottom = top + static_cast<int64_t>(rect_height);
    if (right <= 0 || bottom <= 0 || left >= bounds_width || top >= bounds_height) return false;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > bounds_width) right = bounds_width;
    if (bottom > bounds_height) bottom = bounds_height;
    x = static_cast<int32_t>(left);
    y = static_cast<int32_t>(top);
    rect_width = static_cast<int32_t>(right - left);
    rect_height = static_cast<int32_t>(bottom - top);
    return rect_width > 0 && rect_height > 0;
}

bool clip_rect(int32_t& x, int32_t& y, int32_t& rect_width, int32_t& rect_height) {
    return clip_rect_to_bounds(x, y, rect_width, rect_height,
                               static_cast<int32_t>(width), static_cast<int32_t>(height));
}

bool clipping_safety_contract_ok() {
    int32_t x = -4, y = -4, w = 8, h = 8;
    const bool partial = clip_rect(x, y, w, h) && x == 0 && y == 0 && w == 4 && h == 4;
    x = 2147483644; y = 0; w = 16; h = 1;
    const bool positive_overflow_rejected = !clip_rect(x, y, w, h);
    x = -2147483647 - 1; y = 0; w = 16; h = 1;
    const bool negative_overflow_rejected = !clip_rect(x, y, w, h);
    return partial && positive_overflow_rejected && negative_overflow_rejected;
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
    const uint32_t denominator = rect_height > 1 ? static_cast<uint32_t>(rect_height - 1) : 1U;
    for (int32_t row = 0; row < rect_height; ++row)
        fill_rect(x, y + row, rect_width, 1, mix(top, bottom, static_cast<uint32_t>(row), denominator));
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

int32_t rounded_row_inset(int32_t rect_height, int32_t radius, int32_t row) {
    if (rect_height <= 0 || radius <= 0 || row < 0 || row >= rect_height) return 0;
    const int32_t corner_row = row < radius ? row : (row >= rect_height - radius ? rect_height - row - 1 : radius);
    if (corner_row >= radius) return 0;
    const int32_t dy = radius - corner_row - 1;
    const int32_t radius_squared = radius * radius;
    int32_t span = radius;
    while (span > 0 && span * span + dy * dy > radius_squared) --span;
    return radius - span;
}

void draw_round_border(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t radius, uint32_t color) {
    if (rect_width <= 0 || rect_height <= 0) return;
    const int32_t maximum = (rect_width < rect_height ? rect_width : rect_height) / 2;
    if (radius < 0) radius = 0;
    if (radius > maximum) radius = maximum;
    const int32_t inner_radius = radius > 1 ? radius - 1 : 0;
    for (int32_t row = 0; row < rect_height; ++row) {
        const int32_t outer_inset = rounded_row_inset(rect_height, radius, row);
        const int32_t outer_left = x + outer_inset;
        const int32_t outer_right = x + rect_width - outer_inset - 1;
        if (row == 0 || row + 1 == rect_height || rect_width <= 2 || rect_height <= 2) {
            fill_rect(outer_left, y + row, outer_right - outer_left + 1, 1, color);
            continue;
        }
        const int32_t inner_inset = rounded_row_inset(rect_height - 2, inner_radius, row - 1);
        const int32_t inner_left = x + 1 + inner_inset;
        const int32_t inner_right = x + rect_width - inner_inset - 2;
        if (inner_left > outer_left) fill_rect(outer_left, y + row, inner_left - outer_left, 1, color);
        if (outer_right > inner_right) fill_rect(inner_right + 1, y + row, outer_right - inner_right, 1, color);
    }
}

void draw_shadow(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, int32_t radius) {
    fill_round_rect_alpha(x + 8, y + 12, rect_width, rect_height, radius + 4, rgb(0, 0, 0), 22);
    fill_round_rect_alpha(x + 3, y + 5, rect_width, rect_height, radius + 2, rgb(0, 0, 0), 38);
}

const Glyph* glyph_for(char value) {
    if (value >= 'a' && value <= 'z') {
        const Glyph* lowercase = lowercase_glyph_for(value);
        if (lowercase) return lowercase;
    }
    for (uint32_t index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); ++index)
        if (glyphs[index].value == value) return &glyphs[index];
    for (uint32_t index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); ++index)
        if (glyphs[index].value == '?') return &glyphs[index];
    return nullptr;
}

bool font_contract_ok() {
    const Glyph* lower_a = glyph_for('a');
    const Glyph* upper_a = glyph_for('A');
    return lower_a && upper_a && lower_a != upper_a && lower_a->rows[0] == 0U &&
        upper_a->rows[0] != 0U && glyph_for('g') && glyph_for('z');
}

void draw_char(int32_t x, int32_t y, char value, uint32_t color, uint32_t scale = 1U) {
    const Glyph* glyph = glyph_for(value);
    if (!glyph || !scale) return;
    for (int32_t row = 0; row < 7; ++row) {
        for (int32_t column = 0; column < 5; ++column) {
            const uint8_t coverage = font_soft_coverage(glyph, row, column);
            if (!coverage) continue;
            const int32_t pixel_x = x + column * static_cast<int32_t>(scale);
            const int32_t pixel_y = y + row * static_cast<int32_t>(scale);
            if (coverage == 255U)
                fill_rect(pixel_x, pixel_y, static_cast<int32_t>(scale), static_cast<int32_t>(scale), color);
            else
                fill_rect_alpha(pixel_x, pixel_y, static_cast<int32_t>(scale), static_cast<int32_t>(scale), color, coverage);
        }
    }
}

void draw_text_glyph(int32_t x, int32_t y, const Glyph* glyph, const FontGlyphMetrics& metrics,
                     uint32_t color, uint32_t scale) {
    if (!glyph || !scale || !metrics.width) return;
    for (int32_t row = 0; row < 7; ++row) {
        for (int32_t column = metrics.left; column <= metrics.right; ++column) {
            const uint8_t coverage = font_soft_coverage(glyph, row, column);
            if (!coverage) continue;
            const int32_t pixel_x = x + (column - metrics.left) * static_cast<int32_t>(scale);
            const int32_t pixel_y = y + row * static_cast<int32_t>(scale);
            if (coverage == 255U)
                fill_rect(pixel_x, pixel_y, static_cast<int32_t>(scale), static_cast<int32_t>(scale), color);
            else
                fill_rect_alpha(pixel_x, pixel_y, static_cast<int32_t>(scale), static_cast<int32_t>(scale), color, coverage);
        }
    }
}

void draw_text(int32_t x, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) {
    while (text && *text) {
        const Glyph* glyph = glyph_for(*text++);
        const FontGlyphMetrics metrics = font_glyph_metrics(glyph);
        draw_text_glyph(x, y, glyph, metrics, color, scale);
        x += static_cast<int32_t>(metrics.advance * scale);
    }
}

int32_t text_width(const char* text, uint32_t scale = 1U) {
    uint32_t width_pixels = 0U;
    while (text && *text) width_pixels += font_glyph_metrics(glyph_for(*text++)).advance;
    return static_cast<int32_t>(width_pixels * scale);
}

uint32_t text_prefix_count_for_width(const char* text, int32_t maximum_width, uint32_t scale = 1U) {
    if (!text || maximum_width <= 0 || !scale) return 0U;
    int32_t used = 0;
    uint32_t count = 0U;
    while (text[count]) {
        const FontGlyphMetrics metrics = font_glyph_metrics(glyph_for(text[count]));
        const int32_t advance = static_cast<int32_t>(metrics.advance * scale);
        if (advance <= 0 || used > maximum_width - advance) break;
        used += advance;
        ++count;
    }
    return count;
}

void draw_text_ellipsis(int32_t x, int32_t y, int32_t maximum_width, const char* text,
                        uint32_t color, uint32_t scale = 1U) {
    if (!text || maximum_width <= 0 || !scale) return;
    if (text_width(text, scale) <= maximum_width) { draw_text(x, y, text, color, scale); return; }
    const int32_t ellipsis_width = text_width("...", scale);
    if (ellipsis_width > maximum_width) return;
    const uint32_t count = text_prefix_count_for_width(text, maximum_width - ellipsis_width, scale);
    for (uint32_t index = 0; index < count; ++index) {
        const Glyph* glyph = glyph_for(text[index]);
        const FontGlyphMetrics metrics = font_glyph_metrics(glyph);
        draw_text_glyph(x, y, glyph, metrics, color, scale);
        x += static_cast<int32_t>(metrics.advance * scale);
    }
    draw_text(x, y, "...", color, scale);
}

void draw_text_right_ellipsis(int32_t right, int32_t y, int32_t maximum_width, const char* text,
                              uint32_t color, uint32_t scale = 1U) {
    const int32_t width_pixels = text_width(text, scale);
    if (width_pixels <= maximum_width) draw_text(right - width_pixels, y, text, color, scale);
    else draw_text_ellipsis(right - maximum_width, y, maximum_width, text, color, scale);
}

bool font_metrics_contract_ok() {
    return text_width("ill") < text_width("mmm") && text_width(" ") == 4 && text_width("ii") == 8;
}

bool text_ellipsis_contract_ok() {
    const int32_t dots = text_width("...");
    const int32_t two_i = text_width("ii");
    return dots > 0 && text_prefix_count_for_width("iiii", two_i) == 2U &&
        text_prefix_count_for_width("iiii", 0) == 0U;
}

bool color_mix_contract_ok() {
    return mix(0x102030U, 0xA0B0C0U, 2U, 1U) == 0xA0B0C0U &&
        mix(0x102030U, 0xA0B0C0U, 0U, 0U) == 0x102030U;
}

void draw_text_right(int32_t right, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) { draw_text(right - text_width(text, scale), y, text, color, scale); }
void draw_text_center(int32_t center, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) { draw_text(center - text_width(text, scale) / 2, y, text, color, scale); }

void draw_uint(int32_t x, int32_t y, uint32_t value, uint32_t color) {
    char buffer[11]; uint32_t count = 0;
    do { buffer[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(buffer));
    while (count) { draw_char(x, y, buffer[--count], color, 1); x += 6; }
}

Theme theme_for(uint8_t id) {
    if (id == 1U) return Theme{
        rgb(14,16,21), rgb(24,27,34), rgb(99,108,128), rgb(52,59,72),
        rgb(24,27,34), rgb(34,38,47), rgb(17,19,24), rgb(57,63,75),
        rgb(238,240,245), rgb(151,158,172), rgb(132,148,188), rgb(48,55,70),
        rgb(111,204,161), rgb(227,187,96), rgb(229,107,132), rgb(10,12,16)};
    if (id == 2U) return Theme{
        rgb(24,17,16), rgb(39,27,23), rgb(183,119,65), rgb(112,75,53),
        rgb(38,27,25), rgb(51,36,33), rgb(27,20,19), rgb(76,53,47),
        rgb(246,235,224), rgb(181,157,140), rgb(222,155,84), rgb(78,52,41),
        rgb(126,197,148), rgb(231,190,98), rgb(226,104,114), rgb(15,11,11)};
    return Theme{
        desktop_canvas, rgb(18,23,35), rgb(104,117,226), rgb(70,82,146),
        desktop_surface, rgb(31,37,52), rgb(11,14,22), rgb(54,62,82),
        rgb(240,243,250), rgb(153,163,187), desktop_accent, rgb(48,57,99),
        rgb(105,205,161), rgb(229,190,101), rgb(230,108,137), rgb(8,10,16)};
}

void apply_runtime_theme(uint8_t id) {
    runtime_theme = id > 2U ? 0U : id;
    current_theme = theme_for(runtime_theme);
}
