#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "../tools/zenpkg/sha256.hpp"

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
using uintptr_t = std::uintptr_t;

namespace process {
constexpr uint32_t application_buffer_bytes = 64U * 1024U;
uint8_t application_storage[application_buffer_bytes]{};
uint8_t* application_buffer = application_storage;
}

namespace storage {
bool read_file(const char*, uint8_t*, uint32_t, uint32_t&);
bool write_file(const char*, const uint8_t*, uint32_t, bool);
bool sync_metadata();
bool bytes_equal(const void*, const void*, uint32_t);
}

namespace security_guard {
void sha256(const uint8_t* data, uint32_t size, uint8_t output[32]) {
    zenpkg::Sha256 context;
    context.update(data, size);
    const auto digest = context.final();
    std::memcpy(output, digest.data(), digest.size());
}
}

#include "../kernel/parts/security_policy_format.inc"
#define SECURITY_POLICY_TRANSACTION_HOST_TEST 1
#include "../kernel/parts/security_policy_transaction.inc"

namespace {
template <std::size_t Capacity>
struct File {
    std::array<uint8_t, Capacity> bytes{};
    uint32_t size = 0U;
};

File<256> live_policy;
File<16> live_version;
File<512> live_audit;
File<4096> live_journal;

bool reads_enabled = true;
bool writes_enabled = true;
bool sync_enabled = true;
bool corrupt_journal_readback = false;
bool corrupt_policy_readback = false;
uint32_t sync_count = 0U;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template <std::size_t Capacity>
void set_file(File<Capacity>& file, std::string_view text) {
    require(text.size() <= Capacity, "fixture capacity");
    file.bytes.fill(0U);
    if (!text.empty()) std::memcpy(file.bytes.data(), text.data(), text.size());
    file.size = static_cast<uint32_t>(text.size());
}

template <std::size_t Capacity>
bool copy_out(const File<Capacity>& file, uint8_t* output, uint32_t capacity,
        uint32_t& size, bool corrupt) {
    if (!output || capacity < file.size) return false;
    if (file.size) std::memcpy(output, file.bytes.data(), file.size);
    if (corrupt && file.size) output[file.size - 1U] ^= 0x01U;
    size = file.size;
    return true;
}

template <std::size_t Capacity>
bool replace(File<Capacity>& file, const uint8_t* input, uint32_t size) {
    if (size > Capacity || (!input && size)) return false;
    file.bytes.fill(0U);
    if (size) std::memcpy(file.bytes.data(), input, size);
    file.size = size;
    return true;
}

bool file_equals(const File<256>& file, std::string_view text) {
    return file.size == text.size() &&
        (!file.size || std::memcmp(file.bytes.data(), text.data(), text.size()) == 0);
}
bool file_equals(const File<16>& file, std::string_view text) {
    return file.size == text.size() &&
        (!file.size || std::memcmp(file.bytes.data(), text.data(), text.size()) == 0);
}
bool file_equals(const File<512>& file, std::string_view text) {
    return file.size == text.size() &&
        (!file.size || std::memcmp(file.bytes.data(), text.data(), text.size()) == 0);
}

void seed_clean() {
    set_file(live_policy, "signed-policy-v1");
    set_file(live_version, "1\n");
    set_file(live_audit, "audit-v1");
    live_journal.bytes.fill(0U);
    live_journal.size = 0U;
    reads_enabled = writes_enabled = sync_enabled = true;
    corrupt_journal_readback = corrupt_policy_readback = false;
    sync_count = 0U;
    std::memset(process::application_storage, 0, sizeof(process::application_storage));
    security_policy_transaction::busy = false;
    security_policy_transaction::recovered_domain = security_policy_transaction::Domain::none;
    security_policy_transaction::wipe();
}

void simulate_reboot() {
    security_policy_transaction::busy = false;
    security_policy_transaction::recovered_domain = security_policy_transaction::Domain::none;
    security_policy_transaction::wipe();
    std::memset(process::application_storage, 0, sizeof(process::application_storage));
}

void begin_zmid() {
    uint32_t size = 0U;
    require(security_policy_transaction::acquire(
        "/security/zenovguard-intelligence.zmid", 256U, size), "prepare journal");
    require(size == std::strlen("signed-policy-v1"), "policy backup size");
    require(live_journal.size > sizeof(security_policy_transaction::JournalHeader),
        "journal persisted");
    require(sync_count == 1U, "journal durable before mutation");
}

void mutate_policy() { set_file(live_policy, "signed-policy-v2"); }
void mutate_version() { set_file(live_version, "2\n"); }
void mutate_audit() { set_file(live_audit, "audit-v2"); }

void require_restored(const char* context) {
    require(file_equals(live_policy, "signed-policy-v1"), context);
    require(file_equals(live_version, "1\n"), context);
    require(file_equals(live_audit, "audit-v1"), context);
    require(live_journal.size == 0U, "journal cleared after replay");
    require(security_policy_transaction::recovered_domain ==
        security_policy_transaction::Domain::zmid, "recovered domain");
}

void crash_case(uint32_t stage) {
    seed_clean();
    begin_zmid();
    if (stage >= 1U) mutate_policy();
    if (stage >= 2U) mutate_version();
    if (stage >= 3U) require(storage::sync_metadata(), "live metadata sync");
    if (stage >= 4U) mutate_audit();
    if (stage >= 5U) require(storage::sync_metadata(), "audit metadata sync");
    simulate_reboot();
    require(security_policy_transaction::recover_pending(), "hot journal replay");
    require_restored("crash replay restored exact old generation");
}
}

namespace storage {
bool read_file(const char* path, uint8_t* output, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (!reads_enabled || !path || !output) return false;
    if (std::strcmp(path, "/security/zenovguard-intelligence.zmid") == 0) {
        return copy_out(live_policy, output, capacity, size, corrupt_policy_readback);
    }
    if (std::strcmp(path, "/security/zenovguard-intelligence.version") == 0) {
        return copy_out(live_version, output, capacity, size, false);
    }
    if (std::strcmp(path, "/security/zenovguard.audit") == 0) {
        return copy_out(live_audit, output, capacity, size, false);
    }
    if (std::strcmp(path, "/security/policy-transaction.journal") == 0) {
        return copy_out(live_journal, output, capacity, size, corrupt_journal_readback);
    }
    return false;
}

bool write_file(const char* path, const uint8_t* input, uint32_t size, bool append) {
    if (!writes_enabled || !path || append) return false;
    if (std::strcmp(path, "/security/zenovguard-intelligence.zmid") == 0) {
        return replace(live_policy, input, size);
    }
    if (std::strcmp(path, "/security/zenovguard-intelligence.version") == 0) {
        return replace(live_version, input, size);
    }
    if (std::strcmp(path, "/security/zenovguard.audit") == 0) {
        return replace(live_audit, input, size);
    }
    if (std::strcmp(path, "/security/policy-transaction.journal") == 0) {
        return replace(live_journal, input, size);
    }
    return false;
}

bool sync_metadata() {
    if (!sync_enabled) return false;
    ++sync_count;
    return true;
}

bool bytes_equal(const void* left, const void* right, uint32_t size) {
    return left && right && std::memcmp(left, right, size) == 0;
}
}

int main() {
    try {
        char formatted[12]{};
        uint32_t formatted_size = 0U;
        require(security_policy_format::format_nonzero_decimal_u32(
            0xFFFFFFFFU, formatted, sizeof(formatted), formatted_size), "format max");
        require(formatted_size == 11U &&
            std::memcmp(formatted, "4294967295\n", 11U) == 0, "canonical max");

        seed_clean();
        require(security_policy_transaction::recover_pending(), "clean journal accepted");
        require(security_policy_transaction::recovered_domain ==
            security_policy_transaction::Domain::none, "clean domain none");

        seed_clean();
        begin_zmid();
        uint32_t nested = 0U;
        require(!security_policy_transaction::acquire(
            "/security/zenovguard-intelligence.zmid", 256U, nested), "nested blocked");
        require(security_policy_transaction::discard(), "discard prepared journal");
        require(live_journal.size == 0U && !security_policy_transaction::busy,
            "discard clears durable state");

        seed_clean();
        begin_zmid();
        mutate_policy(); mutate_version(); mutate_audit();
        require(storage::sync_metadata(), "commit live sync");
        require(security_policy_transaction::commit(), "commit marker removal");
        require(file_equals(live_policy, "signed-policy-v2") &&
            file_equals(live_version, "2\n") && file_equals(live_audit, "audit-v2"),
            "commit preserves new generation");
        simulate_reboot();
        require(security_policy_transaction::recover_pending(), "post-commit clean boot");
        require(security_policy_transaction::recovered_domain ==
            security_policy_transaction::Domain::none, "committed journal not replayed");

        for (uint32_t stage = 0U; stage < 6U; ++stage) crash_case(stage);

        seed_clean();
        begin_zmid();
        mutate_policy(); mutate_version(); mutate_audit();
        simulate_reboot();
        corrupt_journal_readback = true;
        require(!security_policy_transaction::recover_pending(), "corrupt journal fail closed");
        require(live_journal.size != 0U, "corrupt journal retained for diagnosis");
        require(file_equals(live_policy, "signed-policy-v2"), "corrupt journal not guessed");
        corrupt_journal_readback = false;

        seed_clean();
        begin_zmid();
        mutate_policy();
        simulate_reboot();
        corrupt_policy_readback = true;
        require(!security_policy_transaction::recover_pending(), "restore readback corruption detected");
        corrupt_policy_readback = false;

        seed_clean();
        writes_enabled = false;
        uint32_t failed_size = 123U;
        require(!security_policy_transaction::acquire(
            "/security/zenovguard-intelligence.zmid", 256U, failed_size) && failed_size == 0U,
            "journal write failure propagated");

        std::cout << "SECURITY_POLICY_TRANSACTION_TEST_OK format=4 backup=bounded nested=blocked readback=verified version=verified wipe=volatile\n";
        std::cout << "SECURITY_POLICY_JOURNAL_TEST_OK schema=1 hot=replayed commit=atomic audit=covered digest=verified corrupt=fail-closed crash-points=6\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "security-policy-journal-test: " << error.what() << '\n';
        return 1;
    }
}
