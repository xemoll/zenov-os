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
}

namespace console {
constexpr uint32_t width = 80;
constexpr uint32_t height = 25;
volatile uint16_t* const vga = reinterpret_cast<volatile uint16_t*>(0xB8000);
uint32_t row = 0;
uint32_t column = 0;
uint8_t color = 0x0A;

uint16_t cell(char c) { return static_cast<uint16_t>(static_cast<uint8_t>(c)) | (static_cast<uint16_t>(color) << 8); }
void set_color(uint8_t foreground, uint8_t background) { color = static_cast<uint8_t>((background << 4) | (foreground & 0x0F)); }
void scroll() {
    if (row < height) return;
    for (uint32_t y = 1; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) vga[(y - 1) * width + x] = vga[y * width + x];
    for (uint32_t x = 0; x < width; ++x) vga[(height - 1) * width + x] = cell(' ');
    row = height - 1;
}
void clear() {
    for (uint32_t i = 0; i < width * height; ++i) vga[i] = cell(' ');
    row = 0; column = 0;
}
void put(char c) {
    if (c == '\r') { column = 0; serial::put(c); return; }
    if (c == '\n') { column = 0; ++row; scroll(); serial::put('\r'); serial::put('\n'); return; }
    if (c == '\b') {
        if (column > 0) { --column; vga[row * width + column] = cell(' '); }
        serial::write("\b \b");
        return;
    }
    if (c == '\t') { do { put(' '); } while ((column & 3U) != 0); return; }
    if (static_cast<unsigned char>(c) < 32) return;
    vga[row * width + column] = cell(c);
    ++column;
    if (column >= width) { column = 0; ++row; scroll(); }
    serial::put(c);
}
void write(const char* text) { while (*text) put(*text++); }
void line(const char* text) { write(text); put('\n'); }
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

