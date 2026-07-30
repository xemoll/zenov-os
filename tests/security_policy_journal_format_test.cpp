#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

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

namespace {
std::array<uint8_t, 32> live_policy{};
std::array<uint8_t, 16> live_version{};
std::array<uint8_t, 4096> live_journal{};
uint32_t live_policy_size = 0U;
uint32_t live_version_size = 0U;
uint32_t live_journal_size = 0U;
bool allow_journal_write = true;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

namespace storage {
bool read_file(const char* path, uint8_t* output, uint32_t capacity, uint32_t& size);
bool write_file(const char* path, const uint8_t* input, uint32_t size, bool append);
bool sync_metadata();
bool bytes_equal(const void* left, const void* right, uint32_t size);
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
void recompute_digest(uint8_t* journal, uint32_t size) {
    auto* header = reinterpret_cast<security_policy_transaction::JournalHeader*>(journal);
    std::memset(header->digest, 0, sizeof(header->digest));
    security_guard::sha256(journal, size, header->digest);
}

uint32_t make_canonical_zgdb_journal(uint8_t* journal) {
    using namespace security_policy_transaction;
    std::memset(journal, 0, process::application_buffer_bytes);
    auto* header = reinterpret_cast<JournalHeader*>(journal);
    header->magic[0] = 'Z'; header->magic[1] = 'P'; header->magic[2] = 'T'; header->magic[3] = 'J';
    header->schema = journal_schema;
    header->header_size = sizeof(JournalHeader);
    header->state = journal_state_prepared;
    header->domain = static_cast<uint8_t>(Domain::zgdb);
    header->policy_size = 4U;
    header->version_size = 2U;
    header->previous_version = 3U;
    header->payload_size = 6U;
    require(copy_text(header->live_path, sizeof(header->live_path), "/security/zenovguard.zgdb"), "copy live path");
    require(copy_text(header->version_path, sizeof(header->version_path), "/security/zenovguard.version"), "copy version path");
    require(copy_text(header->auxiliary_path, sizeof(header->auxiliary_path), nullptr), "copy empty auxiliary path");
    uint8_t* payload = journal + sizeof(JournalHeader);
    payload[0] = 'p'; payload[1] = 'o'; payload[2] = 'l'; payload[3] = '3';
    payload[4] = '3'; payload[5] = '\n';
    const uint32_t size = sizeof(JournalHeader) + header->payload_size;
    recompute_digest(journal, size);
    return size;
}

bool validate(uint8_t* journal, uint32_t size) {
    using namespace security_policy_transaction;
    const Descriptor* descriptor = nullptr;
    const uint8_t *policy = nullptr, *version = nullptr, *auxiliary = nullptr;
    uint32_t previous_version = 0U;
    return validate_journal(journal, size, descriptor, policy, version, auxiliary, previous_version);
}
}

namespace storage {
bool read_file(const char* path, uint8_t* output, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (!path || !output) return false;
    if (std::strcmp(path, "/security/zenovguard.zgdb") == 0) {
        if (capacity < live_policy_size) return false;
        std::memcpy(output, live_policy.data(), live_policy_size); size = live_policy_size; return true;
    }
    if (std::strcmp(path, "/security/zenovguard.version") == 0) {
        if (capacity < live_version_size) return false;
        std::memcpy(output, live_version.data(), live_version_size); size = live_version_size; return true;
    }
    if (std::strcmp(path, "/security/policy-transaction.journal") == 0) {
        if (capacity < live_journal_size) return false;
        if (live_journal_size) std::memcpy(output, live_journal.data(), live_journal_size);
        size = live_journal_size; return true;
    }
    return false;
}

bool write_file(const char* path, const uint8_t* input, uint32_t size, bool append) {
    if (!path || append) return false;
    if (std::strcmp(path, "/security/policy-transaction.journal") != 0 || !allow_journal_write) return false;
    if (size > live_journal.size() || (!input && size)) return false;
    live_journal.fill(0U);
    if (size) std::memcpy(live_journal.data(), input, size);
    live_journal_size = size;
    return true;
}

bool sync_metadata() { return true; }
bool bytes_equal(const void* left, const void* right, uint32_t size) {
    return left && right && std::memcmp(left, right, size) == 0;
}
}

int main() {
    try {
        using namespace security_policy_transaction;
        uint8_t* journal = process::application_buffer;
        const uint32_t size = make_canonical_zgdb_journal(journal);
        require(validate(journal, size), "canonical journal rejected");

        make_canonical_zgdb_journal(journal);
        auto* header = reinterpret_cast<JournalHeader*>(journal);
        std::memset(header->live_path, 'A', sizeof(header->live_path));
        recompute_digest(journal, size);
        require(!validate(journal, size), "unterminated path accepted");

        make_canonical_zgdb_journal(journal);
        header = reinterpret_cast<JournalHeader*>(journal);
        const uint32_t live_length = bounded_text_length(header->live_path, sizeof(header->live_path));
        require(live_length + 1U < sizeof(header->live_path), "fixture path capacity");
        header->live_path[live_length + 1U] = 'X';
        recompute_digest(journal, size);
        require(!validate(journal, size), "nonzero path padding accepted");

        make_canonical_zgdb_journal(journal);
        header = reinterpret_cast<JournalHeader*>(journal);
        header->auxiliary_path[0] = '/';
        recompute_digest(journal, size);
        require(!validate(journal, size), "unexpected auxiliary path accepted");

        make_canonical_zgdb_journal(journal);
        header = reinterpret_cast<JournalHeader*>(journal);
        header->domain = 0xFFU;
        recompute_digest(journal, size);
        require(!validate(journal, size), "unknown domain accepted");

        live_policy.fill(0U); live_version.fill(0U); live_journal.fill(0U);
        std::memcpy(live_policy.data(), "pol3", 4U); live_policy_size = 4U;
        std::memcpy(live_version.data(), "3\n", 2U); live_version_size = 2U;
        live_journal_size = 0U;
        allow_journal_write = false;
        busy = false; wipe();
        uint32_t backup_size = 123U;
        require(!acquire("/security/zenovguard.zgdb", 32U, backup_size), "uncertain prepare reported success");
        require(backup_size == 0U && prepare_uncertain(), "uncertain prepare not exposed");

        std::cout << "SECURITY_POLICY_JOURNAL_FORMAT_OK paths=bounded padding=zeroed domain=closed digest=recomputed prepare=fail-closed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "security-policy-journal-format-test: " << error.what() << '\n';
        return 1;
    }
}
