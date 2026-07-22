#include <cstdint>
#include <cstdio>
#include <cstring>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

namespace storage {
#include "../kernel/parts/storage_block_result.inc"
#include "../kernel/parts/storage_block_device.inc"
}

namespace {
int failures = 0;
int cases = 0;
uint32_t read_calls = 0;
uint32_t write_calls = 0;

void expect(bool condition, const char* label) {
    ++cases;
    if (!condition) {
        std::fprintf(stderr, "BLOCK_DEVICE_ABI_TEST_FAIL case=%s\n", label);
        ++failures;
    }
}

storage::BlockResult read_ok(uint32_t lba, uint8_t* output) {
    ++read_calls;
    output[0] = 0xA5U;
    return storage::block_result(storage::BlockOperation::read, storage::BlockStatus::ok,
                                 lba, 1U, 0x40U, 0U);
}

storage::BlockResult write_ok(uint32_t lba, const uint8_t*) {
    ++write_calls;
    return storage::block_result(storage::BlockOperation::write, storage::BlockStatus::ok,
                                 lba, 1U, 0x40U, 0U);
}

storage::BlockResult read_timeout(uint32_t lba, uint8_t*) {
    ++read_calls;
    return storage::block_result(storage::BlockOperation::read, storage::BlockStatus::timeout_busy,
                                 lba, 2U, 0x80U, 0x04U);
}
}

int main() {
    using namespace storage;
    uint8_t sector[512]{};
    const uint8_t input[512]{};
    BlockIoState state = block_io_state_initial();
    BlockDevice device{"test0", true, true, 8U, read_ok, write_ok};

    expect(block_device_contract_valid(device), "valid-device-contract");
    expect(block_io_counters_valid(state), "initial-counters-valid");
    expect(std::strcmp(block_status_name(BlockStatus::read_only), "read-only") == 0,
           "read-only-status-name");
    expect(!block_retryable(BlockStatus::read_only), "read-only-not-retryable");

    BlockResult result = block_device_read(device, state, 3U, sector);
    expect(result.ok() && result.operation == BlockOperation::read && result.lba == 3U,
           "read-success-result");
    expect(sector[0] == 0xA5U && read_calls == 1U, "read-success-callback");
    expect(state.requests == 1U && state.completed == 1U && state.failed == 0U,
           "read-success-counters");

    result = block_device_write(device, state, 4U, input);
    expect(result.ok() && result.operation == BlockOperation::write && result.lba == 4U,
           "write-success-result");
    expect(write_calls == 1U, "write-success-callback");
    expect(state.requests == 2U && state.completed == 2U && state.failed == 0U,
           "write-success-counters");

    device.present = false;
    result = block_device_read(device, state, 0U, sector);
    expect(result.status == BlockStatus::no_device && read_calls == 1U,
           "read-no-device");
    result = block_device_write(device, state, 0U, input);
    expect(result.status == BlockStatus::no_device && write_calls == 1U,
           "write-no-device");
    device.present = true;

    result = block_device_read(device, state, 0U, nullptr);
    expect(result.status == BlockStatus::invalid_argument && read_calls == 1U,
           "read-null-buffer");
    result = block_device_write(device, state, 0U, nullptr);
    expect(result.status == BlockStatus::invalid_argument && write_calls == 1U,
           "write-null-buffer");

    result = block_device_read(device, state, 8U, sector);
    expect(result.status == BlockStatus::out_of_range && read_calls == 1U,
           "read-out-of-range");
    result = block_device_write(device, state, 8U, input);
    expect(result.status == BlockStatus::out_of_range && write_calls == 1U,
           "write-out-of-range");

    device.writable = false;
    device.write_sector = nullptr;
    result = block_device_write(device, state, 1U, input);
    expect(result.status == BlockStatus::read_only && write_calls == 1U,
           "write-read-only-without-callback");
    device.writable = true;
    device.write_sector = write_ok;

    device.read_sector = nullptr;
    result = block_device_read(device, state, 1U, sector);
    expect(result.status == BlockStatus::no_device, "read-callback-missing");
    device.read_sector = read_timeout;

    result = block_device_read(device, state, 6U, sector);
    expect(result.status == BlockStatus::timeout_busy && result.attempts == 2U,
           "read-timeout-result");
    expect(result.status_register == 0x80U && result.error_register == 0x04U,
           "read-timeout-registers");
    expect(state.last_failure.status == BlockStatus::timeout_busy &&
               state.last_failure.lba == 6U,
           "last-failure-preserved");

    device.read_sector = read_ok;
    result = block_device_read(device, state, 2U, sector);
    expect(result.ok(), "success-after-failure");
    expect(state.last_result.ok() && state.last_result.lba == 2U,
           "last-result-updated");
    expect(state.last_failure.status == BlockStatus::timeout_busy &&
               state.last_failure.lba == 6U,
           "last-failure-retained");
    expect(block_io_counters_valid(state), "final-counters-valid");
    expect(state.requests == state.completed + state.failed && state.failed == 9U,
           "final-counter-values");

    BlockDevice missing_name{nullptr, true, true, 1U, read_ok, write_ok};
    expect(!block_device_contract_valid(missing_name), "missing-name-contract");
    BlockDevice missing_write{"broken-write", true, true, 1U, read_ok, nullptr};
    expect(!block_device_contract_valid(missing_write), "missing-write-contract");
    BlockDevice zero_capacity{"zero", true, false, 0U, read_ok, nullptr};
    expect(!block_device_contract_valid(zero_capacity), "zero-capacity-contract");

    if (failures != 0) return 1;
    std::printf("BLOCK_DEVICE_ABI_TEST_OK cases=%d requests=%u completed=%u failed=%u\n",
                cases, state.requests, state.completed, state.failed);
    return 0;
}
