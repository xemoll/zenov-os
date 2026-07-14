struct [[gnu::packed]] BootInfo { uint32_t magic; uint8_t drive; uint8_t e820_count; uint16_t reserved; };
struct [[gnu::packed]] E820Entry {
    uint32_t base_low, base_high, length_low, length_high, type, attributes;
};
template <typename T> const volatile T* physical(uintptr_t address) {
    asm volatile("" : "+r"(address));
    return reinterpret_cast<const volatile T*>(address);
}
const volatile BootInfo* boot_info() { return physical<BootInfo>(0x0500); }
const volatile E820Entry* e820_entries() { return physical<E820Entry>(0x0600); }
constexpr uint32_t boot_magic = 0x5A4F5331;

struct [[gnu::packed]] IdtEntry { uint16_t low; uint16_t selector; uint8_t zero; uint8_t flags; uint16_t high; };
struct [[gnu::packed]] IdtPointer { uint16_t limit; uint32_t base; };
IdtEntry idt[256];

void idt_set(uint8_t vector, uintptr_t address) {
    idt[vector].low = static_cast<uint16_t>(address & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].flags = 0x8E;
    idt[vector].high = static_cast<uint16_t>(address >> 16);
}
void idt_init() {
    memset(idt, 0, sizeof(idt));
    for (uint32_t i = 0; i < 34; ++i) idt_set(static_cast<uint8_t>(i), reinterpret_cast<uintptr_t>(isr_stub_table[i]));
    const IdtPointer pointer{static_cast<uint16_t>(sizeof(idt) - 1), reinterpret_cast<uint32_t>(&idt[0])};
    asm volatile("lidt %0" : : "m"(pointer));
}

void pic_remap() {
    const uint8_t master_mask = in8(0x21), slave_mask = in8(0xA1);
    out8(0x20, 0x11); io_wait(); out8(0xA0, 0x11); io_wait();
    out8(0x21, 0x20); io_wait(); out8(0xA1, 0x28); io_wait();
    out8(0x21, 0x04); io_wait(); out8(0xA1, 0x02); io_wait();
    out8(0x21, 0x01); io_wait(); out8(0xA1, 0x01); io_wait();
    (void)master_mask; (void)slave_mask;
    out8(0x21, 0xFC);  // timer + keyboard
    out8(0xA1, 0xFF);
}
void pic_eoi(uint8_t irq) { if (irq >= 8) out8(0xA0, 0x20); out8(0x20, 0x20); }

volatile uint32_t timer_ticks = 0;
void pit_init(uint32_t hz) {
    const uint16_t divisor = static_cast<uint16_t>(1193182U / hz);
    out8(0x43, 0x36); out8(0x40, static_cast<uint8_t>(divisor)); out8(0x40, static_cast<uint8_t>(divisor >> 8));
}

constexpr uint16_t key_up = 0x100;
constexpr uint16_t key_down = 0x101;
volatile uint16_t key_queue[128];
volatile uint32_t key_head = 0, key_tail = 0;
bool shift_pressed = false, caps_lock = false, extended_scancode = false;

void key_push(uint16_t key) {
    const uint32_t next = (key_head + 1) % 128;
    if (next != key_tail) { key_queue[key_head] = key; key_head = next; }
}
uint16_t key_pop_blocking() {
    for (;;) {
        disable_interrupts();
        if (key_tail != key_head) {
            const uint16_t key = key_queue[key_tail]; key_tail = (key_tail + 1) % 128; enable_interrupts(); return key;
        }
        asm volatile("sti; hlt; cli");
    }
}
char key_ascii(uint8_t code, bool shifted) {
    switch (code) {
        case 0x02: return shifted ? '!' : '1'; case 0x03: return shifted ? '@' : '2';
        case 0x04: return shifted ? '#' : '3'; case 0x05: return shifted ? '$' : '4';
        case 0x06: return shifted ? '%' : '5'; case 0x07: return shifted ? '^' : '6';
        case 0x08: return shifted ? '&' : '7'; case 0x09: return shifted ? '*' : '8';
        case 0x0A: return shifted ? '(' : '9'; case 0x0B: return shifted ? ')' : '0';
        case 0x0C: return shifted ? '_' : '-'; case 0x0D: return shifted ? '+' : '=';
        case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e'; case 0x13: return 'r'; case 0x14: return 't';
        case 0x15: return 'y'; case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o'; case 0x19: return 'p';
        case 0x1A: return shifted ? '{' : '['; case 0x1B: return shifted ? '}' : ']';
        case 0x1E: return 'a'; case 0x1F: return 's'; case 0x20: return 'd'; case 0x21: return 'f'; case 0x22: return 'g';
        case 0x23: return 'h'; case 0x24: return 'j'; case 0x25: return 'k'; case 0x26: return 'l';
        case 0x27: return shifted ? ':' : ';'; case 0x28: return shifted ? '"' : '\''; case 0x29: return shifted ? '~' : '`';
        case 0x2B: return shifted ? '|' : '\\';
        case 0x2C: return 'z'; case 0x2D: return 'x'; case 0x2E: return 'c'; case 0x2F: return 'v'; case 0x30: return 'b';
        case 0x31: return 'n'; case 0x32: return 'm'; case 0x33: return shifted ? '<' : ','; case 0x34: return shifted ? '>' : '.';
        case 0x35: return shifted ? '?' : '/'; case 0x39: return ' ';
        default: return 0;
    }
}
void keyboard_scancode(uint8_t code) {
    if (code == 0xE0) { extended_scancode = true; return; }
    const bool released = (code & 0x80U) != 0;
    code &= 0x7F;
    if (code == 0x2A || code == 0x36) { shift_pressed = !released; extended_scancode = false; return; }
    if (released) { extended_scancode = false; return; }
    if (extended_scancode) {
        if (code == 0x48) key_push(key_up); else if (code == 0x50) key_push(key_down);
        extended_scancode = false; return;
    }
    if (code == 0x3A) { caps_lock = !caps_lock; return; }
    if (code == 0x1C) { key_push('\n'); return; }
    if (code == 0x0E) { key_push('\b'); return; }
    if (code == 0x0F) { key_push('\t'); return; }
    char value = key_ascii(code, shift_pressed);
    if (value >= 'a' && value <= 'z' && (shift_pressed != caps_lock)) value = static_cast<char>(value - ('a' - 'A'));
    if (value) key_push(static_cast<uint8_t>(value));
}

struct InterruptFrame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t vector, error, eip, cs, eflags;
};

[[noreturn]] void panic(const char* message, const InterruptFrame* frame = nullptr) {
    disable_interrupts();
    console::set_color(15, 4);
    console::line("\n*** ZENOVOS KERNEL PANIC ***");
    console::line(message);
    if (frame) {
        console::write("vector="); console::unsigned_dec(frame->vector);
        console::write(" error="); console::hex(frame->error);
        console::write(" eip="); console::hex(frame->eip); console::put('\n');
    }
    console::line("System halted. Inspect COM1 output for diagnostics.");
    halt_forever();
}

extern "C" void interrupt_dispatch(InterruptFrame* frame) {
    if (frame->vector == 32) { ++timer_ticks; pic_eoi(0); return; }
    if (frame->vector == 33) { keyboard_scancode(in8(0x60)); pic_eoi(1); return; }
    panic("unhandled CPU exception", frame);
}

bool cpuid_available() {
    uint32_t before, after;
    asm volatile("pushfl; popl %0" : "=r"(before));
    const uint32_t toggled = before ^ (1U << 21);
    asm volatile("pushl %1; popfl; pushfl; popl %0" : "=r"(after) : "r"(toggled) : "cc");
    asm volatile("pushl %0; popfl" : : "r"(before));
    return ((before ^ after) & (1U << 21)) != 0;
}
void cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(0));
}

uint8_t cmos_read(uint8_t reg) { out8(0x70, static_cast<uint8_t>(0x80 | reg)); io_wait(); return in8(0x71); }
uint8_t bcd(uint8_t value) { return static_cast<uint8_t>((value & 0x0F) + ((value >> 4) * 10)); }
struct Rtc { uint8_t second, minute, hour, day, month, year; };
Rtc rtc_read() {
    while (cmos_read(0x0A) & 0x80) {}
    Rtc value{cmos_read(0), cmos_read(2), cmos_read(4), cmos_read(7), cmos_read(8), cmos_read(9)};
    const uint8_t status = cmos_read(0x0B);
    if ((status & 0x04) == 0) {
        value.second = bcd(value.second); value.minute = bcd(value.minute); value.hour = bcd(static_cast<uint8_t>(value.hour & 0x7F));
        value.day = bcd(value.day); value.month = bcd(value.month); value.year = bcd(value.year);
    }
    if ((status & 0x02) == 0 && (value.hour & 0x80)) value.hour = static_cast<uint8_t>(((value.hour & 0x7F) + 12) % 24);
    return value;
}
void two_digits(uint8_t value) { console::put(static_cast<char>('0' + value / 10)); console::put(static_cast<char>('0' + value % 10)); }

char history[8][80];
