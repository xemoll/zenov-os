#include <stddef.h>
#include <stdint.h>
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
