#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;
using uint64_t = std::uint64_t;
using int32_t = std::int32_t;

namespace storage {
#include "../kernel/parts/storage_ata_policy.inc"
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "ATA_POLICY_TEST_FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace storage::ata_policy;
    uint32_t cases = 0U;
    auto check = [&](bool condition, const char* message) { ++cases; require(condition, message); };

    check(ticks_from_ms(1U, 100U) == 1U, "sub-tick timeout must round up");
    check(ticks_from_ms(10U, 100U) == 1U, "10ms at 100Hz");
    check(ticks_from_ms(11U, 100U) == 2U, "11ms at 100Hz rounds up");
    check(ticks_from_ms(1000U, 100U) == 100U, "one second at 100Hz");
    check(deadline_after(0xFFFFFFF0U, 200U, 100U) == 4U, "deadline wrap");
    check(!deadline_reached(0xFFFFFFF8U, 4U), "wrapped deadline not reached early");
    check(deadline_reached(4U, 4U), "deadline reached exactly");
    check(deadline_reached(5U, 4U), "deadline reached after wrap");

    check(classify_device_error(0U, 0U) == Error::no_device, "zero status means no device");
    check(classify_device_error(0xFFU, 0U) == Error::no_device, "floating bus means no device");
    check(classify_device_error(status_device_fault, 0U) == Error::device_fault, "device fault");
    check(classify_device_error(status_error, error_interface_crc) == Error::interface_crc, "ICRC mapping");
    check(classify_device_error(status_error, error_uncorrectable) == Error::uncorrectable, "UNC mapping");
    check(classify_device_error(status_error, error_id_not_found) == Error::id_not_found, "IDNF mapping");
    check(classify_device_error(status_error, error_command_aborted) == Error::command_aborted, "ABRT mapping");
    check(classify_device_error(status_device_ready, 0U) == Error::none, "ready status");

    check(retryable(Error::timeout_busy), "busy timeout retryable");
    check(retryable(Error::timeout_drq), "DRQ timeout retryable");
    check(retryable(Error::interface_crc), "interface CRC retryable");
    check(!retryable(Error::uncorrectable), "uncorrectable data is not retryable");
    check(!retryable(Error::out_of_range), "range error is not retryable");
    check(std::string_view(error_name(Error::reset_failed)) == "reset-failed", "stable error name");

    std::cout << "ATA_POLICY_TEST_OK cases=" << cases << '\n';
    return 0;
}
