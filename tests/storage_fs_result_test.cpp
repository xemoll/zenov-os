#include <cstdint>
#include <cstdio>
#include <cstring>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

namespace storage {
#include "../kernel/parts/storage_block_result.inc"
#include "../kernel/parts/storage_fs_result.inc"
}

namespace process {
constexpr uint32_t error_invalid = 0xFFFFFFFFU;
constexpr uint32_t error_not_found = 0xFFFFFFFEU;
constexpr uint32_t error_no_space = 0xFFFFFFFDU;
constexpr uint32_t error_io = 0xFFFFFFFCU;
constexpr uint32_t error_denied = 0xFFFFFFF9U;
constexpr uint32_t error_corrupt = 0xFFFFFFF8U;
#include "../kernel/parts/process_fs_errors.inc"
}

namespace {
int cases = 0;
int failures = 0;

void expect(bool condition, const char* label) {
    ++cases;
    if (!condition) {
        std::fprintf(stderr, "ZENOVFS_RESULT_TEST_FAIL case=%s\n", label);
        ++failures;
    }
}
}

int main() {
    using namespace storage;

    const FsResult success = fs_success(FsOperation::read, 123U);
    expect(success.ok(), "success-ok");
    expect(success.durable(), "success-durable");
    expect(success.bytes == 123U, "success-bytes");
    expect(!success.has_block_error(), "success-no-block-error");

    const BlockResult retry_block = block_result(BlockOperation::read, BlockStatus::timeout_busy,
                                                 7U, 2U, 0x80U, 0x04U);
    const FsResult retry = fs_from_block(FsOperation::read, retry_block, 64U);
    expect(!retry.ok(), "retry-not-ok");
    expect(retry.status == FsStatus::io_error, "retry-io-error");
    expect(retry.has_block_error(), "retry-block-error");
    expect(fs_retryable(retry), "retryable-transport");
    expect(retry.block.lba == 7U && retry.block.attempts == 2U, "retry-context");

    const FsResult medium = fs_from_block(
        FsOperation::write,
        block_result(BlockOperation::write, BlockStatus::uncorrectable, 9U, 1U, 0x51U, 0x40U));
    expect(medium.status == FsStatus::io_error, "medium-io-error");
    expect(!fs_retryable(medium), "medium-not-retryable");

    const FsResult bounds = fs_from_block(
        FsOperation::mount,
        block_result(BlockOperation::read, BlockStatus::out_of_range, 999U, 0U, 0U, 0U));
    expect(bounds.status == FsStatus::metadata_corrupt, "block-range-is-metadata-corrupt");

    const FsResult read_only = fs_from_block(
        FsOperation::write,
        block_result(BlockOperation::write, BlockStatus::read_only, 4U, 0U, 0U, 0U));
    expect(read_only.status == FsStatus::read_only, "read-only-preserved");
    expect(!fs_retryable(read_only), "read-only-not-retryable");

    const FsResult pending = fs_recovery_pending(FsOperation::write, 512U, retry_block);
    expect(pending.ok(), "pending-is-committed-success");
    expect(!pending.durable(), "pending-not-clean-durable");
    expect(pending.status == FsStatus::recovery_pending, "pending-status");

    expect(std::strcmp(fs_operation_name(FsOperation::recover), "recover") == 0,
           "operation-name");
    expect(std::strcmp(fs_status_name(FsStatus::checksum_mismatch), "checksum-mismatch") == 0,
           "checksum-name");
    expect(std::strcmp(fs_status_name(FsStatus::permission_denied), "permission-denied") == 0,
           "denied-name");
    expect(std::strcmp(fs_status_name(FsStatus::recovery_pending), "recovery-pending") == 0,
           "pending-name");

    expect(process::fs_status_error_mapping_valid(), "mapping-self-test");
    expect(process::fs_status_to_syscall_error(FsStatus::not_found) == process::error_not_found,
           "map-not-found");
    expect(process::fs_status_to_syscall_error(FsStatus::buffer_too_small) == process::error_no_space,
           "map-capacity");
    expect(process::fs_status_to_syscall_error(FsStatus::permission_denied) == process::error_denied,
           "map-denied");
    expect(process::fs_status_to_syscall_error(FsStatus::read_only) == process::error_denied,
           "map-read-only");
    expect(process::fs_status_to_syscall_error(FsStatus::checksum_mismatch) == process::error_corrupt,
           "map-checksum");
    expect(process::fs_status_to_syscall_error(FsStatus::metadata_corrupt) == process::error_corrupt,
           "map-metadata");
    expect(process::fs_status_to_syscall_error(FsStatus::io_error) == process::error_io,
           "map-io");
    expect(process::fs_status_to_syscall_error(FsStatus::invalid_path) == process::error_invalid,
           "map-invalid-path");
    expect(process::fs_status_to_syscall_error(FsStatus::wrong_type) == process::error_invalid,
           "map-wrong-type");

    if (failures != 0) return 1;
    std::printf("ZENOVFS_RESULT_TEST_OK cases=%d abi=typed syscall-errors=distinct recovery=pending\n",
                cases);
    return 0;
}
