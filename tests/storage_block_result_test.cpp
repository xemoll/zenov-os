#include <cstdint>
#include <cstdio>
#include <cstring>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

namespace storage {
#include "../kernel/parts/storage_block_result.inc"
}

namespace {
int failures = 0;
int cases = 0;

void expect(bool condition, const char* label) {
    ++cases;
    if (!condition) {
        std::fprintf(stderr, "BLOCK_RESULT_TEST_FAIL case=%s\n", label);
        ++failures;
    }
}
}

int main() {
    using namespace storage;
    const BlockResult success = block_success(BlockOperation::read, 42U, 2U);
    expect(success.ok(), "success-ok");
    expect(success.operation == BlockOperation::read, "success-operation");
    expect(success.lba == 42U && success.attempts == 2U, "success-context");
    expect(std::strcmp(block_operation_name(BlockOperation::flush), "flush") == 0, "operation-name");
    expect(std::strcmp(block_status_name(BlockStatus::command_aborted), "command-aborted") == 0, "status-name");
    expect(std::strcmp(block_status_name(BlockStatus::read_only), "read-only") == 0, "read-only-name");

    const BlockResult failure = block_result(BlockOperation::write, BlockStatus::interface_crc,
                                             7U, 1U, 0x51U, 0x80U);
    expect(!failure.ok(), "failure-not-ok");
    expect(failure.status_register == 0x51U && failure.error_register == 0x80U, "register-context");
    expect(block_retryable(BlockStatus::timeout_busy), "retry-timeout-busy");
    expect(block_retryable(BlockStatus::timeout_drq), "retry-timeout-drq");
    expect(block_retryable(BlockStatus::device_fault), "retry-device-fault");
    expect(block_retryable(BlockStatus::command_aborted), "retry-command-aborted");
    expect(block_retryable(BlockStatus::interface_crc), "retry-interface-crc");
    expect(!block_retryable(BlockStatus::uncorrectable), "no-retry-uncorrectable");
    expect(!block_retryable(BlockStatus::id_not_found), "no-retry-id-not-found");
    expect(!block_retryable(BlockStatus::out_of_range), "no-retry-out-of-range");
    expect(!block_retryable(BlockStatus::invalid_argument), "no-retry-invalid-argument");
    expect(!block_retryable(BlockStatus::read_only), "no-retry-read-only");
    expect(!block_retryable(BlockStatus::reset_failed), "no-retry-reset-failed");

    if (failures != 0) return 1;
    std::printf("BLOCK_RESULT_TEST_OK cases=%d\n", cases);
    return 0;
}
