using uint8_t = unsigned char;
using uint16_t = unsigned short;
using uint32_t = unsigned int;
using uint64_t = unsigned long long;
using int32_t = int;
using int64_t = long long;
using uintptr_t = unsigned int;
using size_t = unsigned int;
static_assert(sizeof(uint8_t) == 1 && sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4 && sizeof(uint64_t) == 8);

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
#include "parts/storage_browser.inc"
namespace security_guard {
struct ScanResult;
bool allow_persistent_write(const char*, const uint8_t*, uint32_t, bool);
bool allow_persistent_transfer(const char*, const char*);
}
namespace zmid {
extern bool ready;
extern uint32_t active_record_count;
bool classify(const char*, const uint8_t*, uint32_t, const uint8_t[32], security_guard::ScanResult&);
bool self_test();
}
namespace process { bool active_security_actor(char[48], uint8_t[32]); }
namespace zrwp {
bool authorize_write(const char*, uint32_t);
bool authorize_copy(const char*, uint32_t);
bool authorize_rename(const char*, const char*);
bool authorize_remove(const char*);
}
namespace storage {
bool quarantine_rename(const char*, const char*);
bool quarantine_write_metadata(const char*, const uint8_t*, uint32_t);
bool quarantine_remove(const char*);
}
#include "parts/security_paths.inc"
namespace package_manager {
bool allow_execution(const char*, const uint8_t*, uint32_t);
bool activate_capabilities(const char*, const uint8_t*, uint32_t);
bool command_token_equal(const char*, const char*);
bool dispatch_line(char*);
int version_compare(const char*, const char*);
}
namespace storage { bool security_read_file(const char*, uint8_t*, uint32_t, uint32_t&); }
#define read_file security_read_file
#define write_file guarded_write_file
#define remove guarded_remove
#define rename_entry guarded_rename_entry
#define copy_file guarded_copy_file
#define syscall_dispatch syscall_dispatch_unrestricted
#define run run_unchecked
#include "parts/process.inc"
#undef run
#undef syscall_dispatch
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
#include "parts/zcap_policy.inc"
#include "parts/zmid_policy.inc"
#include "parts/process_capabilities.inc"
#include "parts/process_package_capabilities.inc"
#include "parts/zrwp_policy.inc"
namespace crypto {
constexpr uint32_t sha256_bytes = security_guard::sha256_bytes;
void sha256(const uint8_t* data, uint32_t size, uint8_t output[sha256_bytes]) { security_guard::sha256(data, size, output); }
bool digest_equal(const uint8_t* left, const uint8_t* right) { return security_guard::digest_equal(left, right); }
bool sha256_self_test() { return security_guard::sha256_self_test(); }
}
#include "parts/rsa_pss.inc"
#include "parts/package_format.inc"
#include "parts/package_repository.inc"
#include "parts/package_manager.inc"
#include "parts/security_io.inc"
#include "parts/process_policy.inc"
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
    if (help) console::line("  Packages     pkg status|list|search|plan|verify|fetch|install|upgrade|repair|policy|info|rollback|remove|run|cache|repo");
}

#include "parts/shell_runtime.inc"

extern "C" void kernel_main() {
    serial::init();
    serial::line("ZENOVOS_BOOT_OK");
    for (uint32_t i = 0; i < zenov_generated::kBootMessageCount; ++i) serial::line(zenov_generated::kBootMessages[i]);
    serial::line("Initializing IDT, memory, storage, audit journal, signed policy, syscall capabilities, signed repository, packages, security, graphics, input and ring-3 services...");

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
    if (!zgdb::init()) panic("Signed ZenovGuard database validation failed.");
    if (!zcap::init()) panic("Signed syscall capability policy validation failed.");
    if (!zmid::init()) panic("Signed malware intelligence validation failed.");
    if (!zrwp::init()) panic("Signed ransomware policy validation failed.");
    if (!security_guard::init()) panic("ZenovGuard cryptographic, intelligence or audit self-test failed.");
    if (!process::capability_init()) panic("Per-application syscall capability policy validation failed.");
    if (!package_repository::init()) panic("Signed ZenRepo metadata validation failed.");
    package_manager::init();
    const uint8_t mutation_probe = 0x5AU;
    if (storage::guarded_write_file("/apps/hello.zex", &mutation_probe, 1U, false)) panic("Trusted application mutation guard failed.");
    if (storage::guarded_write_file("/security/zenovguard.zgdb", &mutation_probe, 1U, false)) panic("Active security database mutation guard failed.");
    if (storage::guarded_write_file("/security/syscall-capabilities.zcap", &mutation_probe, 1U, false)) panic("Active capability policy mutation guard failed.");
    if (storage::guarded_write_file("/security/syscall-capabilities.version", &mutation_probe, 1U, false)) panic("Capability policy version mutation guard failed.");
    if (storage::guarded_write_file("/security/zenovguard-intelligence.zmid", &mutation_probe, 1U, false)) panic("Active malware intelligence mutation guard failed.");
    if (storage::guarded_write_file("/security/zenovguard-intelligence.version", &mutation_probe, 1U, false)) panic("Malware intelligence version mutation guard failed.");
    if (storage::guarded_write_file("/security/ransomware-policy.zrwp", &mutation_probe, 1U, false)) panic("Active ransomware policy mutation guard failed.");
    if (storage::guarded_write_file("/security/ransomware-policy.version", &mutation_probe, 1U, false)) panic("Ransomware policy version mutation guard failed.");
    if (storage::guarded_write_file("/quarantine/security-probe.qtn", &mutation_probe, 1U, false)) panic("Quarantine mutation guard failed.");
    if (storage::guarded_write_file("/security/zenovguard.audit", &mutation_probe, 1U, false)) panic("Persistent security audit mutation guard failed.");
    if (storage::guarded_write_file("/repo/timestamp.zrm", &mutation_probe, 1U, false)) panic("Signed repository metadata mutation guard failed.");
    if (storage::guarded_write_file("/apps/pkg-security-probe.zex", &mutation_probe, 1U, false)) panic("Managed package payload mutation guard failed.");
    if (storage::guarded_write_file("/var/lib/zenpkg/state.v1", &mutation_probe, 1U, false)) panic("Package database mutation guard failed.");
    if (storage::guarded_write_file("/var/lib/zenpkg/repo.v1", &mutation_probe, 1U, false)) panic("Repository anti-rollback state mutation guard failed.");
    if (storage::guarded_write_file("/var/cache/zp/security-probe.part", &mutation_probe, 1U, false)) panic("Package cache mutation guard failed.");
    if (!security_audit::verify_active()) panic("Persistent security audit final-read verification failed.");
    serial::line("ZENOV_GUARD_PROTECTED_PATH_TEST_OK");
    serial::line("ZENOV_GUARD_INTELLIGENCE_PROTECTED_PATH_TEST_OK");
    serial::line("ZRWP_PROTECTED_PATH_TEST_OK");
    serial::line("ZENOV_GUARD_QUARANTINE_PROTECTED_PATH_TEST_OK");
    serial::line("ZENREPO_PROTECTED_PATH_TEST_OK");
    serial::line("ZENPKG_PROTECTED_PATH_TEST_OK");
    serial::line("ZENPKG_CACHE_PROTECTED_PATH_TEST_OK");
    const bool graphical = graphics::init();
    if (graphical) { console::activate_shadow(); serial::line("CONSOLE_SHADOW_OK"); }
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

    serial::line("Kernel online. Desktop, signed policies, persistent audit, syscall capabilities, signed malware intelligence, controlled-folder defense, packages, security, storage and ring-3 services ready.");
    console::show_home();
    if (graphical) graphics::sync_terminal_from_console();
    serial::line("ZENOVOS_UI_READY");
    shell_run();
}
