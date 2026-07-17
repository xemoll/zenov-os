namespace graphics {
constexpr uint32_t width = 800U, height = 600U, bytes_per_pixel = 4U;
constexpr uint32_t framebuffer_bytes = width * height * bytes_per_pixel;
constexpr uint16_t vbe_index_port = 0x01CE, vbe_data_port = 0x01CF;
constexpr uint16_t vbe_id = 0, vbe_xres = 1, vbe_yres = 2, vbe_bpp = 3, vbe_enable = 4, vbe_virtual_width = 6;
constexpr uint16_t vbe_enabled = 0x01, vbe_linear_framebuffer = 0x40;
constexpr uint32_t qemu_vga_id = 0x11111234U;

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
    {':',{0,12,12,0,12,12,0}}, {'/',{1,2,2,4,8,8,16}}, {' ',{0,0,0,0,0,0,0}}
};

volatile uint32_t* framebuffer = nullptr;
uint32_t* backbuffer = nullptr;
uintptr_t physical_framebuffer = 0;
bool active = false, first_mouse_packet = true, dragging = false, drag_reported = false;
int32_t mouse_x = static_cast<int32_t>(width / 2U), mouse_y = static_cast<int32_t>(height / 2U);
int32_t window_x = 150, window_y = 88, drag_offset_x = 0, drag_offset_y = 0;
uint8_t mouse_buttons = 0;

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

uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return (static_cast<uint32_t>(red) << 16U) | (static_cast<uint32_t>(green) << 8U) | blue;
}
uint32_t blend(uint32_t source, uint32_t destination, uint8_t alpha) {
    const uint32_t inverse = 255U - alpha;
    const uint32_t red = ((((source >> 16U) & 0xFFU) * alpha) + (((destination >> 16U) & 0xFFU) * inverse) + 127U) / 255U;
    const uint32_t green = ((((source >> 8U) & 0xFFU) * alpha) + (((destination >> 8U) & 0xFFU) * inverse) + 127U) / 255U;
    const uint32_t blue = (((source & 0xFFU) * alpha) + ((destination & 0xFFU) * inverse) + 127U) / 255U;
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

void stroke_rect(int32_t x, int32_t y, int32_t rect_width, int32_t rect_height, uint32_t color) {
    fill_rect(x, y, rect_width, 1, color); fill_rect(x, y + rect_height - 1, rect_width, 1, color);
    fill_rect(x, y, 1, rect_height, color); fill_rect(x + rect_width - 1, y, 1, rect_height, color);
}

const Glyph* glyph_for(char value) {
    if (value >= 'a' && value <= 'z') value = static_cast<char>(value - ('a' - 'A'));
    for (uint32_t index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); ++index) if (glyphs[index].value == value) return &glyphs[index];
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

void draw_icon(int32_t x, int32_t y, uint32_t background, char letter) {
    fill_rect_alpha(x + 3, y + 4, 38, 38, rgb(0, 0, 0), 70);
    fill_rect(x, y, 38, 38, background); stroke_rect(x, y, 38, 38, blend(rgb(255,255,255), background, 85));
    draw_char(x + 14, y + 11, letter, rgb(245, 248, 252), 2);
}

void draw_file_card(int32_t x, int32_t y, const char* name, uint32_t accent) {
    fill_rect_alpha(x + 4, y + 5, 132, 78, rgb(0,0,0), 28);
    fill_rect(x, y, 132, 78, rgb(250,251,253)); stroke_rect(x, y, 132, 78, rgb(215,221,229));
    fill_rect(x + 14, y + 15, 36, 30, accent); fill_rect(x + 19, y + 11, 18, 8, blend(rgb(255,255,255), accent, 70));
    draw_text(x + 14, y + 57, name, rgb(43,51,64), 1);
}

void render_scene() {
    if (!backbuffer) return;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t red = static_cast<uint8_t>(10U + (y * 16U) / height);
        const uint8_t green = static_cast<uint8_t>(22U + (y * 29U) / height);
        const uint8_t blue = static_cast<uint8_t>(43U + (y * 41U) / height);
        uint32_t* line = backbuffer + y * width;
        const uint32_t color = rgb(red, green, blue);
        for (uint32_t x = 0; x < width; ++x) line[x] = color;
    }

    fill_rect_alpha(0, 0, static_cast<int32_t>(width), 56, rgb(5,10,20), 105);
    draw_text(24, 19, "ZENOVOS", rgb(242,247,255), 2);
    draw_text(700, 22, "0.1.1", rgb(165,181,203), 1);

    fill_rect_alpha(18, 96, 58, 360, rgb(8,14,27), 155); stroke_rect(18, 96, 58, 360, rgb(47,66,91));
    draw_icon(28, 112, rgb(49,115,220), 'F');
    draw_icon(28, 166, rgb(132,83,207), 'T');
    draw_icon(28, 220, rgb(31,153,117), 'S');
    draw_icon(28, 274, rgb(198,103,39), 'A');

    fill_rect_alpha(window_x + 10, window_y + 12, 510, 390, rgb(0,0,0), 95);
    fill_rect(window_x, window_y, 510, 390, rgb(240,244,248));
    stroke_rect(window_x, window_y, 510, 390, rgb(100,118,140));
    fill_rect(window_x, window_y, 510, 40, rgb(29,39,55));
    draw_text(window_x + 18, window_y + 14, "ZENOV FILES", rgb(238,244,252), 1);
    fill_rect(window_x + 442, window_y + 13, 11, 11, rgb(56,180,124));
    fill_rect(window_x + 463, window_y + 13, 11, 11, rgb(236,181,61));
    fill_rect(window_x + 484, window_y + 13, 11, 11, rgb(221,79,79));

    fill_rect(window_x, window_y + 40, 116, 350, rgb(225,232,240));
    draw_text(window_x + 18, window_y + 64, "PLACES", rgb(77,91,111), 1);
    fill_rect(window_x + 10, window_y + 86, 96, 29, rgb(205,217,231));
    draw_text(window_x + 20, window_y + 97, "HOME", rgb(38,49,66), 1);
    draw_text(window_x + 20, window_y + 137, "APPS", rgb(69,82,101), 1);
    draw_text(window_x + 20, window_y + 177, "SYSTEM", rgb(69,82,101), 1);
    draw_text(window_x + 20, window_y + 217, "DOCS", rgb(69,82,101), 1);

    draw_text(window_x + 140, window_y + 65, "HOME", rgb(41,51,65), 2);
    draw_text(window_x + 141, window_y + 88, "ZENOVOS WORKSPACE", rgb(113,126,143), 1);
    draw_file_card(window_x + 140, window_y + 118, "SYSTEM", rgb(65,120,218));
    draw_file_card(window_x + 288, window_y + 118, "APPS", rgb(132,83,207));
    draw_file_card(window_x + 140, window_y + 214, "DOCS", rgb(31,153,117));
    draw_file_card(window_x + 288, window_y + 214, "PROJECTS", rgb(198,103,39));
    fill_rect(window_x + 140, window_y + 318, 280, 1, rgb(205,213,223));
    draw_text(window_x + 140, window_y + 336, "READY - 7 APPLICATIONS", rgb(89,103,122), 1);

    fill_rect_alpha(0, 548, static_cast<int32_t>(width), 52, rgb(8,13,23), 220);
    fill_rect(18, 559, 104, 30, rgb(43,94,171)); draw_text(34, 570, "ZENOVOS", rgb(246,249,253), 1);
    draw_text(164, 570, "FILES", rgb(226,233,243), 1);
    draw_text(250, 570, "TERMINAL", rgb(157,171,190), 1);
    draw_text(382, 570, "SETTINGS", rgb(157,171,190), 1);
    draw_text(704, 570, "READY", rgb(87,205,148), 1);
}

void restore_cursor(int32_t x, int32_t y) {
    if (!active || !backbuffer || !framebuffer) return;
    for (int32_t row = 0; row < 20; ++row) for (int32_t column = 0; column < 14; ++column) {
        const int32_t px = x + column, py = y + row;
        if (px >= 0 && py >= 0 && px < static_cast<int32_t>(width) && py < static_cast<int32_t>(height)) framebuffer[static_cast<uint32_t>(py) * width + static_cast<uint32_t>(px)] = backbuffer[static_cast<uint32_t>(py) * width + static_cast<uint32_t>(px)];
    }
}

void draw_cursor() {
    if (!active || !framebuffer) return;
    for (int32_t row = 0; row < 18; ++row) {
        const int32_t span = row < 12 ? (row / 2 + 2) : (row < 15 ? 5 : 3);
        for (int32_t column = 0; column < span; ++column) {
            const int32_t px = mouse_x + column, py = mouse_y + row;
            if (px < 0 || py < 0 || px >= static_cast<int32_t>(width) || py >= static_cast<int32_t>(height)) continue;
            const bool edge = column == 0 || column + 1 == span || row == 0 || row == 17;
            framebuffer[static_cast<uint32_t>(py) * width + static_cast<uint32_t>(px)] = edge ? rgb(12,18,28) : rgb(245,249,255);
        }
    }
}

void present_full() {
    if (!framebuffer || !backbuffer) return;
    for (uint32_t index = 0; index < width * height; ++index) framebuffer[index] = backbuffer[index];
    draw_cursor();
}

uint32_t scene_hash() {
    uint32_t hash = 2166136261U;
    for (uint32_t index = 0; index < width * height; ++index) { hash ^= backbuffer[index]; hash *= 16777619U; }
    return hash;
}

void serial_hex(uint32_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    serial::write("0x");
    for (int32_t shift = 28; shift >= 0; shift -= 4) serial::put(digits[(value >> static_cast<uint32_t>(shift)) & 0xFU]);
}

bool initialize_mode() {
    const uint16_t identifier = vbe_read(vbe_id);
    if (identifier < 0xB0C0U || identifier > 0xB0C5U) return false;
    vbe_write(vbe_enable, 0);
    vbe_write(vbe_xres, static_cast<uint16_t>(width));
    vbe_write(vbe_yres, static_cast<uint16_t>(height));
    vbe_write(vbe_bpp, 32);
    vbe_write(vbe_virtual_width, static_cast<uint16_t>(width));
    vbe_write(vbe_enable, vbe_enabled | vbe_linear_framebuffer);
    return vbe_read(vbe_xres) == width && vbe_read(vbe_yres) == height && vbe_read(vbe_bpp) == 32 && (vbe_read(vbe_enable) & vbe_enabled);
}

bool init() {
    physical_framebuffer = find_qemu_framebuffer();
    if (!physical_framebuffer) { serial::line("GRAPHICS_PCI_NOT_FOUND"); return false; }
    serial::line("GRAPHICS_PCI_OK");
    if (!paging::map_graphics_window(physical_framebuffer, framebuffer_bytes)) { serial::line("FRAMEBUFFER_MAP_FAILED"); return false; }
    framebuffer = reinterpret_cast<volatile uint32_t*>(paging::graphics_virtual_base);
    serial::line("FRAMEBUFFER_MAPPED_OK");
    if (heap::free_bytes() < framebuffer_bytes + 32768U) { serial::line("BACKBUFFER_NO_MEMORY"); return false; }
    backbuffer = static_cast<uint32_t*>(heap::allocate(framebuffer_bytes, 4096U));
    if (!backbuffer || !initialize_mode()) { serial::line("GRAPHICS_MODE_FAILED"); return false; }
    active = true;
    render_scene(); present_full();
    serial::line("GRAPHICS_MODE_OK 800x600x32");
    serial::line("BACKBUFFER_PRESENT_OK");
    const uint32_t before = backbuffer[0]; fill_rect(-4, -4, 8, 8, rgb(255,0,0)); const bool clipping = backbuffer[0] == rgb(255,0,0); backbuffer[0] = before;
    serial::line(clipping ? "CLIPPING_OK" : "CLIPPING_FAILED");
    serial::line(blend(rgb(255,255,255), rgb(0,0,0), 128) == rgb(128,128,128) ? "ALPHA_BLEND_OK" : "ALPHA_BLEND_FAILED");
    serial::line(glyph_for('Z') && glyph_for('0') ? "FONT_RENDER_OK" : "FONT_RENDER_FAILED");
    serial::write("DESKTOP_SCENE_HASH="); serial_hex(scene_hash()); serial::write("\r\n");
    serial::line("DESKTOP_SCENE_OK");
    return clipping;
}

void on_mouse_packet(int32_t delta_x, int32_t delta_y, uint8_t buttons) {
    if (!active) return;
    restore_cursor(mouse_x, mouse_y);
    mouse_x += delta_x;
    mouse_y += delta_y;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= static_cast<int32_t>(width)) mouse_x = static_cast<int32_t>(width) - 1;
    if (mouse_y >= static_cast<int32_t>(height)) mouse_y = static_cast<int32_t>(height) - 1;
    const bool left = (buttons & 1U) != 0, previous_left = (mouse_buttons & 1U) != 0;
    if (left && !previous_left && mouse_x >= window_x && mouse_x < window_x + 510 && mouse_y >= window_y && mouse_y < window_y + 40) {
        dragging = true; drag_offset_x = mouse_x - window_x; drag_offset_y = mouse_y - window_y;
    }
    if (!left) dragging = false;
    if (dragging) {
        window_x = mouse_x - drag_offset_x;
        window_y = mouse_y - drag_offset_y;
        if (window_x < 88) window_x = 88;
        if (window_x > static_cast<int32_t>(width) - 530) window_x = static_cast<int32_t>(width) - 530;
        if (window_y < 62) window_y = 62;
        if (window_y > 148) window_y = 148;
        render_scene(); present_full();
        if (!drag_reported) { serial::line("WINDOW_DRAG_OK"); drag_reported = true; }
    } else draw_cursor();
    mouse_buttons = buttons;
    if (first_mouse_packet) { serial::line("MOUSE_PACKET_OK"); first_mouse_packet = false; }
}
}
