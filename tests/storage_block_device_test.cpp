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
int cases = 0;
int failures = 0;

void expect(bool condition, const char* label) {
    ++cases;
    if (!condition) {
        std::fprintf(stderr, "BLOCK_DEVICE_ABI_TEST_FAIL case=%s\n", label);
        ++failures;
    }
}

storage::BlockResult fake_read(uint32_t lba, uint8_t* output) {
    if (!output) {
        return storage::block_result(storage::BlockOperation::read,
                                     storage::BlockStatus::invalid_argument,
                                     lba, 0U, 0U, 0U);
    }
    output[0] = 0x5AU;
    return storage::block_success(storage::BlockOperation::read, lba, 1U);
}

storage::BlockResult fake_write(uint32_t lba, const uint8_t* input) {
    if (!input) {
        return storage::block_result(storage::BlockOperation::write,
                                     storage::BlockStatus::invalid_argument,
                                     lba, 0U, 0U, 0U);
    }
    return storage::block_result(storage::BlockOperation::write,
                                 storage::BlockStatus::command_aborted,
                                 lba, 2U, 0x41U, 0x04U);
}
}

int main() {
    using namespace storage;

    BlockDevice device{"test0", true, true, 4096U, fake_read, fake_write};
    expect(block_device_contract_valid(device), "valid-device");

    uint8_t buffer[512]{};
    const BlockResult read = device.read_sector(17U, buffer);
    expect(read.ok(), "typed-read-success");
    expect(read.operation == BlockOperation::read, "read-operation");
    expect(read.lba == 17U, "read-lba");
    expect(read.attempts == 1U, "read-attempts");
    expect(buffer[0] == 0x5AU, "read-payload");

    const BlockResult write = device.write_sector(29U, buffer);
    expect(!write.ok(), "typed-write-failure");
    expect(write.status == BlockStatus::command_aborted, "write-status");
    expect(write.operation == BlockOperation::write, "write-operation");
    expect(write.lba == 29U, "write-lba");
    expect(write.attempts == 2U, "write-attempts");
    expect(write.status_register == 0x41U && write.error_register == 0x04U, "write-registers");

    BlockDevice missing_name{nullptr, true, true, 1U, fake_read, fake_write};
    expect(!block_device_contract_valid(missing_name), "missing-name");

    BlockDevice missing_read{"broken-read", true, false, 1U, nullptr, nullptr};
    expect(!block_device_contract_valid(missing_read), "missing-read");

    BlockDevice missing_write{"broken-write", true, true, 1U, fake_read, nullptr};
    expect(!block_device_contract_valid(missing_write), "missing-write");

    BlockDevice zero_capacity{"zero", true, false, 0U, fake_read, nullptr};
    expect(!block_device_contract_valid(zero_capacity), "online-zero-capacity");

    BlockDevice offline{"offline", false, false, 0U, fake_read, nullptr};
    expect(block_device_contract_valid(offline), "offline-contract");

    const BlockResult invalid = device.read_sector(31U, nullptr);
    expect(invalid.status == BlockStatus::invalid_argument, "invalid-read-propagation");

    if (failures != 0) return 1;
    std::printf("BLOCK_DEVICE_ABI_TEST_OK cases=%d\n", cases);
    return 0;
}
