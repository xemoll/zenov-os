#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

namespace storage {
bool read_file(const char*, uint8_t*, uint32_t, uint32_t&);
bool write_file(const char*, const uint8_t*, uint32_t, bool);
bool bytes_equal(const void*, const void*, uint32_t);
}

#include "../kernel/parts/security_policy_format.inc"
#define SECURITY_POLICY_TRANSACTION_HOST_TEST 1
#include "../kernel/parts/security_policy_transaction.inc"

namespace {
uint8_t live_policy[64]{};
uint32_t live_policy_size = 0U;
uint8_t live_version[16]{};
uint32_t live_version_size = 0U;
bool read_enabled = true;
bool write_enabled = true;
bool corrupt_policy_readback = false;
bool corrupt_version_readback = false;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void set_bytes(uint8_t* output, uint32_t capacity, uint32_t& size, const char* text) {
    const auto length = static_cast<uint32_t>(std::strlen(text));
    require(length <= capacity, "fixture capacity");
    std::memset(output, 0, capacity);
    std::memcpy(output, text, length);
    size = length;
}

void seed_policy(const char* text) { set_bytes(live_policy, sizeof(live_policy), live_policy_size, text); }
void seed_version(const char* text) { set_bytes(live_version, sizeof(live_version), live_version_size, text); }
}

namespace storage {
bool read_file(const char* path, uint8_t* output, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (!read_enabled || !path || !output) return false;
    const uint8_t* source = nullptr;
    uint32_t source_size = 0U;
    bool corrupt = false;
    if (std::strcmp(path, "/security/policy") == 0) {
        source = live_policy; source_size = live_policy_size; corrupt = corrupt_policy_readback;
    } else if (std::strcmp(path, "/security/policy.version") == 0) {
        source = live_version; source_size = live_version_size; corrupt = corrupt_version_readback;
    } else {
        return false;
    }
    if (capacity < source_size) return false;
    std::memcpy(output, source, source_size);
    if (corrupt && source_size) output[source_size - 1U] ^= 0x01U;
    size = source_size;
    return true;
}

bool write_file(const char* path, const uint8_t* input, uint32_t size, bool append) {
    if (!write_enabled || !path || !input || append) return false;
    if (std::strcmp(path, "/security/policy") == 0) {
        if (size > sizeof(live_policy)) return false;
        std::memset(live_policy, 0, sizeof(live_policy));
        std::memcpy(live_policy, input, size);
        live_policy_size = size;
        return true;
    }
    if (std::strcmp(path, "/security/policy.version") == 0) {
        if (size > sizeof(live_version)) return false;
        std::memset(live_version, 0, sizeof(live_version));
        std::memcpy(live_version, input, size);
        live_version_size = size;
        return true;
    }
    return false;
}

bool bytes_equal(const void* left, const void* right, uint32_t size) {
    return left && right && std::memcmp(left, right, size) == 0;
}
}

int main() {
    try {
        char formatted[12]{};
        uint32_t formatted_size = 0U;
        require(security_policy_format::format_nonzero_decimal_u32(1U, formatted, sizeof(formatted), formatted_size), "format one");
        require(formatted_size == 2U && std::memcmp(formatted, "1\n", 2U) == 0, "canonical one");
        require(security_policy_format::format_nonzero_decimal_u32(0xFFFFFFFFU, formatted, sizeof(formatted), formatted_size), "format max");
        require(formatted_size == 11U && std::memcmp(formatted, "4294967295\n", 11U) == 0, "canonical max");
        require(!security_policy_format::format_nonzero_decimal_u32(0U, formatted, sizeof(formatted), formatted_size), "reject zero format");
        char short_buffer[10]{};
        require(!security_policy_format::format_nonzero_decimal_u32(0xFFFFFFFFU, short_buffer, sizeof(short_buffer), formatted_size), "reject short format buffer");

        seed_policy("signed-policy-v1");
        seed_version("1\n");
        uint32_t backup_size = 0U;
        require(security_policy_transaction::acquire("/security/policy", sizeof(live_policy), backup_size), "acquire backup");
        require(backup_size == std::strlen("signed-policy-v1"), "backup size");
        uint32_t nested_size = 0U;
        require(!security_policy_transaction::acquire("/security/policy", sizeof(live_policy), nested_size), "reject nested transaction");

        seed_policy("signed-policy-v2");
        uint8_t readback[64]{};
        require(security_policy_transaction::restore_verified("/security/policy", backup_size, readback, sizeof(readback)), "restore and verify");
        require(live_policy_size == std::strlen("signed-policy-v1") &&
            std::memcmp(live_policy, "signed-policy-v1", live_policy_size) == 0, "restored bytes");
        require(security_policy_transaction::version_matches("/security/policy.version", 1U), "version matches");
        require(!security_policy_transaction::version_matches("/security/policy.version", 2U), "wrong version rejected");

        corrupt_policy_readback = true;
        require(!security_policy_transaction::restore_verified("/security/policy", backup_size, readback, sizeof(readback)), "detect corrupt readback");
        corrupt_policy_readback = false;
        corrupt_version_readback = true;
        require(!security_policy_transaction::version_matches("/security/policy.version", 1U), "detect corrupt version readback");
        corrupt_version_readback = false;

        security_policy_transaction::release();
        require(!security_policy_transaction::busy, "release busy flag");
        for (uint32_t i = 0U; i < security_policy_transaction::workspace_bytes; ++i)
            require(security_policy_transaction::rollback_data[i] == 0U, "release wipes backup");

        read_enabled = false;
        backup_size = 123U;
        require(!security_policy_transaction::acquire("/security/policy", sizeof(live_policy), backup_size) &&
            backup_size == 0U && !security_policy_transaction::busy, "read failure releases transaction");
        read_enabled = true;
        require(!security_policy_transaction::acquire("/security/policy", security_policy_transaction::workspace_bytes + 1U, backup_size),
            "reject oversized backup");
        require(security_policy_transaction::acquire("/security/policy", sizeof(live_policy), backup_size), "reacquire");
        write_enabled = false;
        require(!security_policy_transaction::restore_verified("/security/policy", backup_size, readback, sizeof(readback)), "surface restore write failure");
        write_enabled = true;
        security_policy_transaction::release();

        std::cout << "SECURITY_POLICY_TRANSACTION_TEST_OK format=4 backup=bounded nested=blocked readback=verified version=verified wipe=volatile\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "security-policy-transaction-test: " << error.what() << '\n';
        return 1;
    }
}
