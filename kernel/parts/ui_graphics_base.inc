constexpr uint32_t width = 896U, height = 504U, bytes_per_pixel = 4U;
constexpr uint32_t backbuffer_bytes = width * height * bytes_per_pixel;
constexpr uint16_t vbe_index_port = 0x01CE, vbe_data_port = 0x01CF;
constexpr uint16_t vbe_id = 0, vbe_xres = 1, vbe_yres = 2, vbe_bpp = 3, vbe_enable = 4, vbe_virtual_width = 6, vbe_video_memory_64k = 0x0A;
constexpr uint16_t vbe_enabled = 0x01, vbe_linear_framebuffer = 0x40;
constexpr uint32_t qemu_vga_id = 0x11111234U;
constexpr int32_t ui_grid = 8;
constexpr int32_t window_width = 512, window_height = 400, titlebar_height = 40;
constexpr int32_t topbar_x = 0, topbar_y = 0, topbar_width = 0;
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
    {1600, 900}, {1600, 1200}, {1920, 1080}, {1920, 1200},
    {2048, 1080}, {2048, 1152}, {2048, 1536}, {2560, 1080},
    {2560, 1440}, {2560, 1600}, {3840, 2160}, {4096, 2160}
};
constexpr uint8_t display_mode_count = static_cast<uint8_t>(sizeof(display_modes) / sizeof(display_modes[0]));
constexpr uint8_t default_display_mode = 7U;
constexpr uint32_t maximum_framebuffer_bytes = 4096U * 2160U * bytes_per_pixel;
constexpr uint32_t fallback_framebuffer_bytes = 16U * 1024U * 1024U;
uint32_t available_framebuffer_bytes = 0U;
uint8_t requested_display_mode = default_display_mode;
uint8_t active_display_mode = default_display_mode;
uint32_t display_width = display_modes[default_display_mode].width;
uint32_t display_height = display_modes[default_display_mode].height;
int32_t viewport_x = 0, viewport_y = 0, viewport_width = static_cast<int32_t>(display_width), viewport_height = static_cast<int32_t>(display_height);

bool display_mode_valid(uint8_t mode) { return mode < display_mode_count; }
bool display_mode_bytes(const DisplayMode& mode, uint32_t& bytes) {
    const uint64_t required = static_cast<uint64_t>(mode.width) *
        static_cast<uint64_t>(mode.height) * bytes_per_pixel;
    if (!required || required > 0xFFFFFFFFULL) return false;
    bytes = static_cast<uint32_t>(required);
    return true;
}
uint32_t display_framebuffer_bytes() {
    uint32_t bytes = 0U;
    return display_mode_bytes(display_modes[active_display_mode], bytes) ? bytes : 0U;
}

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

struct Glyph { uint8_t value, width, advance; uint32_t regular[9]; uint32_t strong[9]; };

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
int32_t window_x = 192, window_y = 36, drag_offset_x = 0, drag_offset_y = 0;
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

__attribute__((noinline)) uint32_t color_channel_luminance_10000(uint8_t value) {
    const uint32_t x = static_cast<uint32_t>(value) * 257U;
    uint32_t result = (3053U * x + 32767U) / 65535U + 6822U;
    result = (result * x + 32767U) / 65535U + 125U;
    return (result * x + 32767U) / 65535U;
}

__attribute__((noinline)) uint32_t color_luminance_10000(uint32_t color) {
    const uint32_t red = color_channel_luminance_10000(static_cast<uint8_t>(color >> 16U));
    const uint32_t green = color_channel_luminance_10000(static_cast<uint8_t>(color >> 8U));
    const uint32_t blue = color_channel_luminance_10000(static_cast<uint8_t>(color));
    return (red * 2126U + green * 7152U + blue * 722U + 5000U) / 10000U;
}

__attribute__((noinline)) uint32_t contrast_ratio_x100(uint32_t first, uint32_t second) {
    const uint32_t first_luminance = color_luminance_10000(first);
    const uint32_t second_luminance = color_luminance_10000(second);
    const uint32_t lighter = first_luminance > second_luminance ? first_luminance : second_luminance;
    const uint32_t darker = first_luminance > second_luminance ? second_luminance : first_luminance;
    return ((lighter + 500U) * 100U) / (darker + 500U);
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

enum class UiFontWeight : uint8_t { regular, strong };

const Glyph* glyph_for(char value) {
    uint8_t code = static_cast<uint8_t>(value);
    if (code < ui_font_first || code > ui_font_last) code = static_cast<uint8_t>('?');
    const Glyph* glyph = &ui_glyphs[static_cast<uint32_t>(code - ui_font_first)];
    return glyph->value == code ? glyph : nullptr;
}

uint8_t font_coverage(const Glyph* glyph, int32_t row, int32_t column, UiFontWeight weight) {
    if (!glyph || row < 0 || row >= ui_font_height || column < 0 || column >= glyph->width) return 0U;
    const uint32_t packed = weight == UiFontWeight::strong
        ? glyph->strong[static_cast<uint32_t>(row)]
        : glyph->regular[static_cast<uint32_t>(row)];
    return static_cast<uint8_t>(((packed >> static_cast<uint32_t>(column * 4)) & 0x0FU) * 17U);
}

struct FontGlyphMetrics {
    uint8_t width;
    uint8_t advance;
};

FontGlyphMetrics font_glyph_metrics(const Glyph* glyph) {
    return glyph ? FontGlyphMetrics{glyph->width, glyph->advance} : FontGlyphMetrics{0U, 3U};
}

uint32_t font_ink_sum(const Glyph* glyph, UiFontWeight weight) {
    uint32_t sum = 0U;
    if (!glyph) return sum;
    for (int32_t row = 0; row < ui_font_height; ++row)
        for (int32_t column = 0; column < glyph->width; ++column)
            sum += font_coverage(glyph, row, column, weight);
    return sum;
}

bool font_contract_ok() {
    if (ui_glyph_count != static_cast<uint32_t>(ui_font_last - ui_font_first + 1U)) return false;
    for (uint32_t index = 0U; index < ui_glyph_count; ++index) {
        const Glyph& glyph = ui_glyphs[index];
        if (glyph.value != static_cast<uint8_t>(ui_font_first + index) ||
            glyph.width > ui_font_max_width || glyph.advance == 0U) return false;
    }
    return glyph_for('a') && glyph_for('A') && glyph_for('g') && glyph_for('z') &&
        glyph_for('@') && glyph_for('|');
}

bool font_weight_contract_ok() {
    const Glyph* upper = glyph_for('A');
    const Glyph* lower = glyph_for('m');
    return font_ink_sum(upper, UiFontWeight::strong) > font_ink_sum(upper, UiFontWeight::regular) &&
        font_ink_sum(lower, UiFontWeight::strong) > font_ink_sum(lower, UiFontWeight::regular);
}

void draw_char_weight(int32_t x, int32_t y, char value, uint32_t color,
                      UiFontWeight weight, uint32_t scale = 1U) {
    const Glyph* glyph = glyph_for(value);
    if (!glyph || !scale) return;
    for (int32_t row = 0; row < ui_font_height; ++row) {
        for (int32_t column = 0; column < glyph->width; ++column) {
            const uint8_t coverage = font_coverage(glyph, row, column, weight);
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

void draw_char(int32_t x, int32_t y, char value, uint32_t color, uint32_t scale = 1U) {
    draw_char_weight(x, y, value, color,
                     scale > 1U ? UiFontWeight::strong : UiFontWeight::regular, scale);
}

void draw_text_glyph(int32_t x, int32_t y, const Glyph* glyph, const FontGlyphMetrics& metrics,
                     uint32_t color, UiFontWeight weight, uint32_t scale) {
    if (!glyph || !scale || !metrics.width) return;
    for (int32_t row = 0; row < ui_font_height; ++row) {
        for (int32_t column = 0; column < glyph->width; ++column) {
            const uint8_t coverage = font_coverage(glyph, row, column, weight);
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

void draw_text_weight(int32_t x, int32_t y, const char* text, uint32_t color,
                      UiFontWeight weight, uint32_t scale = 1U) {
    while (text && *text) {
        const Glyph* glyph = glyph_for(*text++);
        const FontGlyphMetrics metrics = font_glyph_metrics(glyph);
        draw_text_glyph(x, y, glyph, metrics, color, weight, scale);
        x += static_cast<int32_t>(metrics.advance * scale);
    }
}

void draw_text(int32_t x, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) {
    draw_text_weight(x, y, text, color,
                     scale > 1U ? UiFontWeight::strong : UiFontWeight::regular, scale);
}

void draw_text_strong(int32_t x, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) {
    draw_text_weight(x, y, text, color, UiFontWeight::strong, scale);
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

void draw_text_ellipsis_weight(int32_t x, int32_t y, int32_t maximum_width, const char* text,
                               uint32_t color, UiFontWeight weight, uint32_t scale = 1U) {
    if (!text || maximum_width <= 0 || !scale) return;
    if (text_width(text, scale) <= maximum_width) {
        draw_text_weight(x, y, text, color, weight, scale);
        return;
    }
    const int32_t ellipsis_width = text_width("...", scale);
    if (ellipsis_width > maximum_width) return;
    const uint32_t count = text_prefix_count_for_width(text, maximum_width - ellipsis_width, scale);
    for (uint32_t index = 0; index < count; ++index) {
        const Glyph* glyph = glyph_for(text[index]);
        const FontGlyphMetrics metrics = font_glyph_metrics(glyph);
        draw_text_glyph(x, y, glyph, metrics, color, weight, scale);
        x += static_cast<int32_t>(metrics.advance * scale);
    }
    draw_text_weight(x, y, "...", color, weight, scale);
}

void draw_text_ellipsis(int32_t x, int32_t y, int32_t maximum_width, const char* text,
                        uint32_t color, uint32_t scale = 1U) {
    draw_text_ellipsis_weight(x, y, maximum_width, text, color,
                              scale > 1U ? UiFontWeight::strong : UiFontWeight::regular, scale);
}

void draw_text_strong_ellipsis(int32_t x, int32_t y, int32_t maximum_width, const char* text,
                               uint32_t color, uint32_t scale = 1U) {
    draw_text_ellipsis_weight(x, y, maximum_width, text, color, UiFontWeight::strong, scale);
}

void draw_text_right_ellipsis(int32_t right, int32_t y, int32_t maximum_width, const char* text,
                              uint32_t color, uint32_t scale = 1U) {
    const int32_t width_pixels = text_width(text, scale);
    if (width_pixels <= maximum_width) draw_text(right - width_pixels, y, text, color, scale);
    else draw_text_ellipsis(right - maximum_width, y, maximum_width, text, color, scale);
}

bool font_metrics_contract_ok() {
    if (!(text_width("ill") < text_width("mmm")) || text_width(" ") != 3) return false;
    for (char digit = '0'; digit <= '9'; ++digit)
        if (font_glyph_metrics(glyph_for(digit)).advance != 6U) return false;
    return text_width("00:00") == 27 && text_width("Settings") < 40;
}

bool text_ellipsis_contract_ok() {
    const int32_t dots = text_width("...");
    const int32_t two_i = text_width("ii");
    return dots > 0 && text_prefix_count_for_width("iiii", two_i) == 2U &&
        text_prefix_count_for_width("iiii", 0) == 0U;
}

void draw_text_right(int32_t right, int32_t y, const char* text, uint32_t color, uint32_t scale = 1U) {
    draw_text(right - text_width(text, scale), y, text, color, scale);
}

void draw_uint(int32_t x, int32_t y, uint32_t value, uint32_t color) {
    char buffer[11]; uint32_t count = 0;
    do { buffer[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(buffer));
    while (count) {
        const char digit = buffer[--count];
        draw_char(x, y, digit, color, 1U);
        x += static_cast<int32_t>(font_glyph_metrics(glyph_for(digit)).advance);
    }
}

bool color_mix_contract_ok() {
    return mix(0x102030U, 0xA0B0C0U, 2U, 1U) == 0xA0B0C0U &&
        mix(0x102030U, 0xA0B0C0U, 0U, 0U) == 0x102030U;
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
