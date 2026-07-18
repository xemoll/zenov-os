using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using int32_t = int;
using uintptr_t = unsigned int;
using size_t = unsigned int;
static_assert(sizeof(uint8_t) == 1 && sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4);

#include "generated/zenov_config.hpp"

#include "parts/core.inc"
#include "parts/memory_compare.inc"
#include "parts/hardware.inc"
#include "parts/memory.inc"
#include "parts/graphics_mapping.inc"
#include "parts/user_window.inc"
#include "parts/storage.inc"
#include "parts/storage_bytes.inc"
#include "parts/storage_tools.inc"
#include "parts/security_paths.inc"
namespace package_manager { bool allow_execution(const char*, const uint8_t*, uint32_t); }
namespace storage { bool security_read_file(const char*, uint8_t*, uint32_t, uint32_t&); }
#define read_file security_read_file
#define write_file guarded_write_file
#define remove guarded_remove
#define rename_entry guarded_rename_entry
#define copy_file guarded_copy_file
#define run run_unchecked
#include "parts/process.inc"
#undef run
#undef copy_file
#undef rename_entry
#undef remove
#undef write_file
#undef read_file
namespace process { constexpr uint32_t application_buffer_bytes = 64U * 1024U; }
namespace security_audit { bool append(uint32_t, uint8_t, uint8_t, const char*, const uint8_t[32]); }
#include "parts/security_guard.inc"
#include "parts/security_audit.inc"
#include "parts/zgdb_policy.inc"
namespace crypto {
constexpr uint32_t sha256_bytes = security_guard::sha256_bytes;
void sha256(const uint8_t* data, uint32_t size, uint8_t output[sha256_bytes]) { security_guard::sha256(data, size, output); }
bool digest_equal(const uint8_t* left, const uint8_t* right) { return security_guard::digest_equal(left, right); }
bool sha256_self_test() { return security_guard::sha256_self_test(); }
}
#include "parts/security_io.inc"
#include "parts/process_policy.inc"
#include "parts/package_format.inc"
#include "parts/package_manager.inc"
#include "parts/graphics.inc"
#include "parts/mouse_regression.inc"
#include "parts/input_v2.inc"
#define write_file guarded_write_file
#define remove guarded_remove
#define rename_entry guarded_rename_entry
#define copy_file guarded_copy_file
#define history shell_history
#define history_count shell_history_count
#define shell_run shell_run_legacy_80
#define execute execute_without_packages
#include "parts/commands.inc"
#undef execute
#undef shell_run
#undef history_count
#undef history
#undef copy_file
#undef rename_entry
#undef remove
#undef write_file
#include "parts/security_commands.inc"

void execute(char* line) {
    const bool help = package_manager::command_token_equal(line, "help");
    if (package_manager::dispatch_line(line)) return;
    execute_without_packages(line);
    if (help) console::line("  Packages     pkg status|list|verify|install|info|rollback|remove|run");
}

#include "parts/shell_runtime.inc"

extern "C" void kernel_main() {
    serial::init();
    serial::line("ZENOVOS_BOOT_OK");
    for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) serial::line(zenov_generated::kBootMessages[i]);
    serial::line("Initializing IDT, memory, storage, audit journal, signed policy, security, packages, graphics, input and ring-3 services...");

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
    if (!security_audit::init()) panic("Persistent ZenovGuard audit journal validation failed.");
    if (!security_guard::init()) panic("ZenovGuard cryptographic or audit self-test failed.");
    if (!zgdb::init()) panic("Signed ZenovGuard database validation failed.");
    const uint8_t mutation_probe = 0x5AU;
    if (storage::guarded_write_file("/apps/hello.zex", &mutation_probe, 1U, false)) panic("Trusted application mutation guard failed.");
    if (storage::guarded_write_file("/security/zenovguard.zgdb", &mutation_probe, 1U, false)) panic("Active security database mutation guard failed.");
    if (storage::guarded_write_file("/security/zenovguard.audit", &mutation_probe, 1U, false)) panic("Persistent security audit mutation guard failed.");
    if (storage::guarded_write_file("/apps/pkg-security-probe.zex", &mutation_probe, 1U, false)) panic("Managed package payload mutation guard failed.");
    if (storage::guarded_write_file("/var/lib/zenpkg/state.v1", &mutation_probe, 1U, false)) panic("Package database mutation guard failed.");
    if (!security_audit::verify_active()) panic("Persistent security audit final-read verification failed.");
    serial::line("ZENOV_GUARD_PROTECTED_PATH_TEST_OK");
    serial::line("ZENPKG_PROTECTED_PATH_TEST_OK");
    package_manager::init();
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

    serial::line("Kernel online. Desktop, signed policy, persistent audit, security, packages, storage and ring-3 services ready.");
    console::show_home();
    serial::line("ZENOVOS_UI_READY");
    shell_run();
}
