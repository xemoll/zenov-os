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
#include "parts/graphics_mapping.inc"
#include "parts/user_window.inc"
#include "parts/storage.inc"
#include "parts/storage_tools.inc"
#define run run_unchecked
#include "parts/process.inc"
#undef run
#include "parts/security_guard.inc"
#include "parts/process_policy.inc"
#include "parts/graphics.inc"
#include "parts/mouse_regression.inc"
#include "parts/input_v2.inc"
#define history shell_history
#define history_count shell_history_count
#define shell_run shell_run_legacy_80
#include "parts/commands.inc"
#undef shell_run
#undef history_count
#undef history
#include "parts/security_commands.inc"
#include "parts/shell_runtime.inc"

extern "C" void kernel_main() {
    serial::init();
    serial::line("ZENOVOS_BOOT_OK");
    for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) serial::line(zenov_generated::kBootMessages[i]);
    serial::line("Initializing IDT, memory, storage, security, graphics, input and ring-3 services...");

    console::set_color(zenov_generated::kForeground, zenov_generated::kBackground);
    idt_init();
    pmm::init();
    paging::init();
    if (!paging::scrub_process_window(true)) panic("User process window scrub self-test failed.");
    serial::line("USER_WINDOW_SCRUB_OK");
    if (!process::elf_policy_self_test()) panic("ELF W^X policy self-test failed.");
    serial::line("ELF_WX_POLICY_OK");
    storage::init();
    process::init();
    if (!security_guard::init()) panic("ZenovGuard cryptographic self-test failed.");
    const bool graphical = graphics::init();
    serial::line(graphical ? "GRAPHICAL_DESKTOP_READY" : "GRAPHICS_FALLBACK_TEXT");
    pic_remap();
    const bool mouse_ready = mouse_init();
    if (!mouse_ready) serial::line("PS2_MOUSE_UNAVAILABLE");
    if (mouse_ready && !mouse_irq_route_ready()) panic("PS/2 mouse IRQ route validation failed.");
    if (mouse_ready) serial::line("PS2_MOUSE_IRQ_ROUTE_OK");
    pit_init(100);
    enable_interrupts();
    if (graphical && mouse_ready && !mouse_decoder_regression()) panic("PS/2 mouse decoder regression failed.");
    if (graphical && mouse_ready) serial::line("PS2_MOUSE_DECODER_OK");

    serial::line("Kernel online. Desktop, security, persistent storage and ring-3 services ready.");
    console::show_home();
    serial::line("ZENOVOS_UI_READY");
    shell_run();
}
