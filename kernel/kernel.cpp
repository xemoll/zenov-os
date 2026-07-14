using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using int32_t = int;
using uintptr_t = unsigned int;
using size_t = unsigned int;
static_assert(sizeof(uint8_t) == 1 && sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4);

#include "generated/zenov_config.hpp"

#include "parts/core.inc"
#include "parts/hardware.inc"
#include "parts/input.inc"
#include "parts/commands.inc"

extern "C" void kernel_main() {
    serial::init();
    console::set_color(zenov_generated::kForeground, zenov_generated::kBackground);
    console::clear();
    serial::write("ZENOVOS_BOOT_OK\r\n");
    for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) console::line(zenov_generated::kBootMessages[i]);
    console::line("Initializing IDT, PIC, PIT and keyboard IRQ...");
    idt_init(); pic_remap(); pit_init(100); enable_interrupts();
    console::line("Kernel online. Protected-mode services ready.");
    command_info();
    shell_run();
}
