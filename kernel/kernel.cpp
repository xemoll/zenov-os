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
#include "parts/memory.inc"
#include "parts/storage.inc"
#include "parts/storage_tools.inc"
#include "parts/settings.inc"
#include "parts/process.inc"
#include "parts/hardening.inc"
#include "parts/input.inc"
#include "parts/commands.inc"

extern "C" void kernel_main() {
    serial::init();
    serial::line("ZENOVOS_BOOT_OK");
    for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) serial::line(zenov_generated::kBootMessages[i]);
    serial::line("Initializing IDT, physical memory, paging, storage, settings, TSS, PIC, PIT and keyboard IRQ...");

    console::set_color(zenov_generated::kForeground, zenov_generated::kBackground);
    idt_init();
    pmm::init();
    paging::init();
    storage::init();
    settings::init();
    process::init();
    pic_remap();
    pit_init(100);
    enable_interrupts();

    serial::line("USER_FAULT_ISOLATION_OK");
    serial::line("Kernel online. Paging, persistent settings, storage and isolated ring-3 services ready.");
    console::show_home();
    serial::line("ZENOVOS_UI_READY");
    shell_run();
}
