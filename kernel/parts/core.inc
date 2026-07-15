extern "C" void* isr_stub_table[];
extern "C" char __kernel_start;
extern "C" char __kernel_file_end;
extern "C" char __kernel_end;

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<unsigned char>(value);
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}
extern "C" void* memmove(void* destination, const void* source, size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    if (out < in) for (size_t i = 0; i < count; ++i) out[i] = in[i];
    else for (size_t i = count; i > 0; --i) out[i - 1] = in[i - 1];
    return destination;
}

namespace {

inline void out8(uint16_t port, uint8_t value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
inline uint8_t in8(uint16_t port) { uint8_t value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
inline void io_wait() { out8(0x80, 0); }
inline void enable_interrupts() { asm volatile("sti"); }
inline void disable_interrupts() { asm volatile("cli"); }
inline void halt_once() { asm volatile("hlt"); }
[[noreturn]] void halt_forever() { disable_interrupts(); for (;;) halt_once(); }

size_t string_length(const char* text) { size_t n = 0; while (text && text[n]) ++n; return n; }
int string_compare(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
bool string_equal(const char* a, const char* b) { return string_compare(a, b) == 0; }
void string_copy(char* destination, const char* source, size_t capacity) {
    if (capacity == 0) return;
    size_t i = 0;
    while (source[i] && i + 1 < capacity) { destination[i] = source[i]; ++i; }
    destination[i] = 0;
}
bool string_starts_with(const char* text, const char* prefix) {
    while (*prefix) if (*text++ != *prefix++) return false;
    return true;
}
bool is_space(char c) { return c == ' ' || c == '\t'; }
char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
void lower_in_place(char* text) { while (*text) { *text = ascii_lower(*text); ++text; } }

namespace serial {
constexpr uint16_t base = 0x3F8;
void init() {
    out8(base + 1, 0x00); out8(base + 3, 0x80); out8(base + 0, 0x03);
    out8(base + 1, 0x00); out8(base + 3, 0x03); out8(base + 2, 0xC7); out8(base + 4, 0x0B);
}
void put(char c) {
    for (uint32_t timeout = 0; timeout < 100000 && (in8(base + 5) & 0x20) == 0; ++timeout) {}
    out8(base, static_cast<uint8_t>(c));
}
void write(const char* text) { while (*text) put(*text++); }
void line(const char* text) { write(text); write("\r\n"); }
}

extern volatile uint32_t timer_ticks;

namespace console {
constexpr uint32_t width = 80;
constexpr uint32_t height = 25;
constexpr uint32_t content_top = 3;
constexpr uint32_t content_bottom = 24;

struct Palette {
    uint8_t header_fg;
    uint8_t header_bg;
    uint8_t body_fg;
    uint8_t body_bg;
    uint8_t accent;
    uint8_t muted;
    uint8_t good;
    uint8_t warning;
};

Palette palette{15, 1, 7, 0, 11, 8, 10, 14};
volatile uint16_t* const vga = reinterpret_cast<volatile uint16_t*>(0xB8000);
uint32_t row = content_top;
uint32_t column = 0;
uint8_t color = 0x07;

uint16_t styled_cell(char c, uint8_t foreground, uint8_t background) {
    const uint8_t attribute = static_cast<uint8_t>((background << 4) | (foreground & 0x0F));
    return static_cast<uint16_t>(static_cast<uint8_t>(c)) | (static_cast<uint16_t>(attribute) << 8);
}
uint16_t cell(char c) { return static_cast<uint16_t>(static_cast<uint8_t>(c)) | (static_cast<uint16_t>(color) << 8); }
void set_color(uint8_t foreground, uint8_t background) { color = static_cast<uint8_t>((background << 4) | (foreground & 0x0F)); }
void update_cursor() {
    const uint16_t position = static_cast<uint16_t>(row * width + column);
    out8(0x3D4, 0x0F); out8(0x3D5, static_cast<uint8_t>(position & 0xFF));
    out8(0x3D4, 0x0E); out8(0x3D5, static_cast<uint8_t>((position >> 8) & 0xFF));
}
void move_to(uint32_t target_row, uint32_t target_column) {
    row = target_row < height ? target_row : height - 1;
    column = target_column < width ? target_column : width - 1;
    update_cursor();
}
void fill_row(uint32_t target_row, char value, uint8_t foreground, uint8_t background) {
    for (uint32_t x = 0; x < width; ++x) vga[target_row * width + x] = styled_cell(value, foreground, background);
}
void write_at(uint32_t target_row, uint32_t target_column, const char* text, uint8_t foreground, uint8_t background) {
    uint32_t x = target_column;
    while (*text && x < width) vga[target_row * width + x++] = styled_cell(*text++, foreground, background);
}
void write_uint_at(uint32_t target_row, uint32_t target_column, uint32_t value, uint8_t foreground, uint8_t background) {
    char buffer[11]; uint32_t count = 0;
    do { buffer[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && count < sizeof(buffer));
    uint32_t x = target_column;
    while (count && x < width) vga[target_row * width + x++] = styled_cell(buffer[--count], foreground, background);
}
void draw_panel(uint32_t x, uint32_t y, uint32_t panel_width, uint32_t panel_height, const char* title_text) {
    if (panel_width < 4 || panel_height < 3 || x + panel_width > width || y + panel_height > content_bottom) return;
    for (uint32_t col = 0; col < panel_width; ++col) {
        vga[y * width + x + col] = styled_cell(col == 0 || col + 1 == panel_width ? '+' : '-', palette.muted, palette.body_bg);
        vga[(y + panel_height - 1) * width + x + col] = styled_cell(col == 0 || col + 1 == panel_width ? '+' : '-', palette.muted, palette.body_bg);
    }
    for (uint32_t line = 1; line + 1 < panel_height; ++line) {
        vga[(y + line) * width + x] = styled_cell('|', palette.muted, palette.body_bg);
        vga[(y + line) * width + x + panel_width - 1] = styled_cell('|', palette.muted, palette.body_bg);
    }
    write_at(y, x + 2, title_text, palette.accent, palette.body_bg);
}
void refresh_status() {
    fill_row(1, ' ', palette.body_fg, palette.body_bg);
    write_at(1, 1, "SESSION  LOCAL", palette.body_fg, palette.body_bg);
    write_at(1, 31, "i686 / protected", palette.muted, palette.body_bg);
    write_at(1, 61, "READY", palette.good, palette.body_bg);
    write_at(1, 69, "UP", palette.muted, palette.body_bg);
    write_uint_at(1, 72, timer_ticks / 100U, palette.body_fg, palette.body_bg);
    write_at(1, 78, "s", palette.muted, palette.body_bg);
}
void draw_chrome() {
    fill_row(0, ' ', palette.header_fg, palette.header_bg);
    write_at(0, 1, "ZENOVOS", palette.header_fg, palette.header_bg);
    write_at(0, 11, "/", palette.muted, palette.header_bg);
    write_at(0, 13, "SYSTEM CONSOLE", palette.header_fg, palette.header_bg);
    const size_t version_length = string_length(zenov_generated::kVersion);
    const uint32_t version_column = version_length + 2 < width ? width - static_cast<uint32_t>(version_length) - 2 : 70;
    write_at(0, version_column, zenov_generated::kVersion, palette.header_fg, palette.header_bg);
    refresh_status();
    fill_row(2, '-', palette.muted, palette.body_bg);
    fill_row(24, ' ', palette.header_fg, palette.header_bg);
    write_at(24, 1, "F1 Help  F2 System  F3 Files  F4 Home  Tab Complete  Up/Down History", palette.header_fg, palette.header_bg);
}
void scroll() {
    if (row < content_bottom) return;
    for (uint32_t y = content_top + 1; y < content_bottom; ++y)
        for (uint32_t x = 0; x < width; ++x) vga[(y - 1) * width + x] = vga[y * width + x];
    for (uint32_t x = 0; x < width; ++x) vga[(content_bottom - 1) * width + x] = cell(' ');
    row = content_bottom - 1;
}
void clear_content() {
    set_color(palette.body_fg, palette.body_bg);
    for (uint32_t y = content_top; y < content_bottom; ++y)
        for (uint32_t x = 0; x < width; ++x) vga[y * width + x] = cell(' ');
    move_to(content_top, 0);
}
void clear() { draw_chrome(); clear_content(); }
bool select_theme(const char* name) {
    if (string_equal(name, "midnight")) palette = Palette{15, 1, 7, 0, 11, 8, 10, 14};
    else if (string_equal(name, "graphite")) palette = Palette{15, 0, 7, 0, 11, 8, 10, 14};
    else if (string_equal(name, "amber")) palette = Palette{15, 6, 7, 0, 14, 8, 10, 11};
    else return false;
    set_color(palette.body_fg, palette.body_bg);
    draw_chrome();
    return true;
}
void put(char c) {
    if (c == '\r') { column = 0; serial::put(c); update_cursor(); return; }
    if (c == '\n') { column = 0; ++row; scroll(); serial::put('\r'); serial::put('\n'); update_cursor(); return; }
    if (c == '\b') {
        if (column > 0) { --column; vga[row * width + column] = cell(' '); }
        serial::write("\b \b");
        update_cursor();
        return;
    }
    if (c == '\t') { do { put(' '); } while ((column & 3U) != 0); return; }
    if (static_cast<unsigned char>(c) < 32) return;
    vga[row * width + column] = cell(c);
    ++column;
    if (column >= width) { column = 0; ++row; scroll(); }
    serial::put(c);
    update_cursor();
}
void write(const char* text) { while (*text) put(*text++); }
void line(const char* text) { write(text); put('\n'); }
void prompt(const char* text) {
    refresh_status();
    set_color(palette.accent, palette.body_bg);
    write(text);
    set_color(palette.body_fg, palette.body_bg);
}
void title(const char* text) {
    set_color(palette.accent, palette.body_bg);
    line(text);
    set_color(palette.body_fg, palette.body_bg);
}
void success(const char* text) {
    set_color(palette.good, palette.body_bg);
    line(text);
    set_color(palette.body_fg, palette.body_bg);
}
void warning(const char* text) {
    set_color(palette.warning, palette.body_bg);
    line(text);
    set_color(palette.body_fg, palette.body_bg);
}
void unsigned_dec(uint32_t value) {
    char buffer[11]; uint32_t count = 0;
    do { buffer[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && count < sizeof(buffer));
    while (count) put(buffer[--count]);
}
void signed_dec(int32_t value) {
    if (value < 0) { put('-'); const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1U; unsigned_dec(magnitude); }
    else unsigned_dec(static_cast<uint32_t>(value));
}
void hex(uint32_t value, uint8_t digits = 8) {
    static constexpr char table[] = "0123456789ABCDEF";
    write("0x");
    for (int shift = static_cast<int>(digits - 1) * 4; shift >= 0; shift -= 4) put(table[(value >> shift) & 0xF]);
}
void show_home();
}
