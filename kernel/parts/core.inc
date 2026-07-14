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

namespace console {
constexpr uint32_t width = 80;
constexpr uint32_t height = 25;
constexpr uint32_t content_top = 3;
constexpr uint32_t content_bottom = 24;
constexpr uint8_t header_background = 1;
constexpr uint8_t header_foreground = 15;
constexpr uint8_t body_foreground = 7;
constexpr uint8_t body_background = 0;
constexpr uint8_t accent_foreground = 11;
constexpr uint8_t muted_foreground = 8;
volatile uint16_t* const vga = reinterpret_cast<volatile uint16_t*>(0xB8000);
uint32_t row = content_top;
uint32_t column = 0;
uint8_t color = static_cast<uint8_t>((body_background << 4) | body_foreground);

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
void draw_chrome() {
    fill_row(0, ' ', header_foreground, header_background);
    write_at(0, 1, "ZenovOS", header_foreground, header_background);
    write_at(0, 11, "SYSTEM CONSOLE", 7, header_background);
    const size_t version_length = string_length(zenov_generated::kVersion);
    const uint32_t version_column = version_length + 2 < width ? width - static_cast<uint32_t>(version_length) - 2 : 70;
    write_at(0, version_column, zenov_generated::kVersion, header_foreground, header_background);

    fill_row(1, ' ', body_foreground, body_background);
    write_at(1, 1, "Local session", body_foreground, body_background);
    write_at(1, 59, "i686 protected mode", muted_foreground, body_background);
    fill_row(2, '-', muted_foreground, body_background);

    fill_row(24, ' ', 7, header_background);
    write_at(24, 1, "help  commands   info  system   clear  reset view   history  recall", 7, header_background);
}
void scroll() {
    if (row < content_bottom) return;
    for (uint32_t y = content_top + 1; y < content_bottom; ++y)
        for (uint32_t x = 0; x < width; ++x) vga[(y - 1) * width + x] = vga[y * width + x];
    for (uint32_t x = 0; x < width; ++x) vga[(content_bottom - 1) * width + x] = cell(' ');
    row = content_bottom - 1;
}
void clear_content() {
    for (uint32_t y = content_top; y < content_bottom; ++y)
        for (uint32_t x = 0; x < width; ++x) vga[y * width + x] = cell(' ');
    move_to(content_top, 0);
}
void clear() {
    draw_chrome();
    clear_content();
}
void show_home() {
    set_color(body_foreground, body_background);
    clear();
    write_at(4, 2, "ZenovOS", accent_foreground, body_background);
    write_at(4, 11, zenov_generated::kVersion, body_foreground, body_background);
    write_at(6, 2, "Status", muted_foreground, body_background);
    write_at(6, 13, "Ready", 10, body_background);
    write_at(7, 2, "Mode", muted_foreground, body_background);
    write_at(7, 13, "i686 protected", body_foreground, body_background);
    write_at(9, 2, "Type help for commands. Use Up/Down to recall history.", body_foreground, body_background);
    move_to(11, 0);
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
    set_color(accent_foreground, body_background);
    write(text);
    set_color(body_foreground, body_background);
}
void title(const char* text) {
    set_color(accent_foreground, body_background);
    line(text);
    set_color(body_foreground, body_background);
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
}
