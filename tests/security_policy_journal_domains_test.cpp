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

File<256> zgdb_policy, zcap_policy, zmid_policy;
File<16> zgdb_version, zcap_version, zmid_version;
File<512> audit;
File<4096> journal;
uint32_t sync_count = 0U;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template <std::size_t Capacity>
void set_file(File<Capacity>& file, std::string_view value) {
    require(value.size() <= Capacity, "fixture overflow");
    file.bytes.fill(0U);
    if (!value.empty()) std::memcpy(file.bytes.data(), value.data(), value.size());
    file.size = static_cast<uint32_t>(value.size());
}

template <std::size_t Capacity>
bool copy_out(const File<Capacity>& file, uint8_t* output, uint32_t capacity, uint32_t& size) {
    if (!output || capacity < file.size) return false;
    if (file.size) std::memcpy(output, file.bytes.data(), file.size);
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

template <std::size_t Capacity>
bool equals(const File<Capacity>& file, std::string_view expected) {
    return file.size == expected.size() &&
        (!file.size || std::memcmp(file.bytes.data(), expected.data(), expected.size()) == 0);
}

void reset_runtime() {
    std::memset(process::application_storage, 0, sizeof(process::application_storage));
    security_policy_transaction::busy = false;
    security_policy_transaction::recovered_domain = security_policy_transaction::Domain::none;
    security_policy_transaction::wipe();
}

void seed() {
    set_file(zgdb_policy, "zgdb-v3"); set_file(zgdb_version, "3\n");
    set_file(zcap_policy, "zcap-v1"); set_file(zcap_version, "1\n");
    set_file(zmid_policy, "zmid-v1"); set_file(zmid_version, "1\n");
    set_file(audit, "audit-v1");
    journal.bytes.fill(0U); journal.size = 0U;
    sync_count = 0U;
    reset_runtime();
}

const security_policy_transaction::JournalHeader& journal_header() {
    require(journal.size >= sizeof(security_policy_transaction::JournalHeader), "journal header missing");
    return *reinterpret_cast<const security_policy_transaction::JournalHeader*>(journal.bytes.data());
}

void verify_domain(const char* live_path, security_policy_transaction::Domain expected_domain,
        File<256>& policy, File<16>& version, std::string_view old_policy,
        std::string_view old_version, std::string_view new_policy, std::string_view new_version,
        bool has_auxiliary) {
    uint32_t size = 0U;
    require(security_policy_transaction::acquire(live_path, 256U, size), "domain journal prepare");
    require(size == old_policy.size(), "domain policy backup size");
    require(sync_count == 1U, "prepared journal synchronized");
    const auto& header = journal_header();
    require(header.domain == static_cast<uint8_t>(expected_domain), "journal domain mismatch");
    require(header.auxiliary_size == (has_auxiliary ? audit.size : 0U), "auxiliary size mismatch");
    require((header.auxiliary_path[0] != 0) == has_auxiliary, "auxiliary path canonicality");

    set_file(policy, new_policy);
    set_file(version, new_version);
    if (has_auxiliary) set_file(audit, "audit-v2");
    reset_runtime();
    require(security_policy_transaction::recover_pending(), "domain hot replay");
    require(security_policy_transaction::recovered_domain == expected_domain, "recovered domain mismatch");
    require(equals(policy, old_policy), "policy not restored");
    require(equals(version, old_version), "version not restored");
    if (has_auxiliary) require(equals(audit, "audit-v1"), "audit not restored");
    require(journal.size == 0U, "journal not cleared");
}
}

namespace storage {
bool read_file(const char* path, uint8_t* output, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (!path || !output) return false;
    if (std::strcmp(path, "/security/zenovguard.zgdb") == 0) return copy_out(zgdb_policy, output, capacity, size);
    if (std::strcmp(path, "/security/zenovguard.version") == 0) return copy_out(zgdb_version, output, capacity, size);
    if (std::strcmp(path, "/security/syscall-capabilities.zcap") == 0) return copy_out(zcap_policy, output, capacity, size);
    if (std::strcmp(path, "/security/syscall-capabilities.version") == 0) return copy_out(zcap_version, output, capacity, size);
    if (std::strcmp(path, "/security/zenovguard-intelligence.zmid") == 0) return copy_out(zmid_policy, output, capacity, size);
    if (std::strcmp(path, "/security/zenovguard-intelligence.version") == 0) return copy_out(zmid_version, output, capacity, size);
    if (std::strcmp(path, "/security/zenovguard.audit") == 0) return copy_out(audit, output, capacity, size);
    if (std::strcmp(path, "/security/policy-transaction.journal") == 0) return copy_out(journal, output, capacity, size);
    return false;
}

bool write_file(const char* path, const uint8_t* input, uint32_t size, bool append) {
    if (!path || append) return false;
    if (std::strcmp(path, "/security/zenovguard.zgdb") == 0) return replace(zgdb_policy, input, size);
    if (std::strcmp(path, "/security/zenovguard.version") == 0) return replace(zgdb_version, input, size);
    if (std::strcmp(path, "/security/syscall-capabilities.zcap") == 0) return replace(zcap_policy, input, size);
    if (std::strcmp(path, "/security/syscall-capabilities.version") == 0) return replace(zcap_version, input, size);
    if (std::strcmp(path, "/security/zenovguard-intelligence.zmid") == 0) return replace(zmid_policy, input, size);
    if (std::strcmp(path, "/security/zenovguard-intelligence.version") == 0) return replace(zmid_version, input, size);
    if (std::strcmp(path, "/security/zenovguard.audit") == 0) return replace(audit, input, size);
    if (std::strcmp(path, "/security/policy-transaction.journal") == 0) return replace(journal, input, size);
    return false;
}

bool sync_metadata() { ++sync_count; return true; }
bool bytes_equal(const void* left, const void* right, uint32_t size) {
    return left && right && std::memcmp(left, right, size) == 0;
}
}

int main() {
    try {
        seed();
        verify_domain("/security/zenovguard.zgdb", security_policy_transaction::Domain::zgdb,
            zgdb_policy, zgdb_version, "zgdb-v3", "3\n", "zgdb-v4", "4\n", false);
        seed();
        verify_domain("/security/syscall-capabilities.zcap", security_policy_transaction::Domain::zcap,
            zcap_policy, zcap_version, "zcap-v1", "1\n", "zcap-v2", "2\n", false);
        seed();
        verify_domain("/security/zenovguard-intelligence.zmid", security_policy_transaction::Domain::zmid,
            zmid_policy, zmid_version, "zmid-v1", "1\n", "zmid-v2", "2\n", true);
        std::cout << "SECURITY_POLICY_JOURNAL_DOMAINS_OK domains=3 optional-aux=canonical replay=exact audit=covered\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "security-policy-journal-domains-test: " << error.what() << '\n';
        return 1;
    }
}
