#include <cstdint>
#include <cstdio>
#include <cstring>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

namespace storage {
#include "../kernel/parts/storage_block_result.inc"
#include "../kernel/parts/storage_fs_result.inc"
#include "../kernel/parts/storage_fs_surface.inc"
}

namespace {
int cases = 0;
int failures = 0;

void expect(bool condition, const char* label) {
    ++cases;
    if (!condition) {
        std::fprintf(stderr, "ZENOVFS_SURFACE_TEST_FAIL case=%s\n", label);
        ++failures;
    }
}
}

int main() {
    using namespace storage;
    expect(fs_surface_contract_valid(), "contract");
    expect(fs_surface_class(FsStatus::ok) == FsSurfaceClass::success, "success");
    expect(fs_surface_class(FsStatus::recovery_pending) == FsSurfaceClass::recovery, "recovery");
    expect(fs_surface_class(FsStatus::invalid_path) == FsSurfaceClass::caller, "caller-path");
    expect(fs_surface_class(FsStatus::wrong_type) == FsSurfaceClass::caller, "caller-type");
    expect(fs_surface_class(FsStatus::not_found) == FsSurfaceClass::missing, "missing-file");
    expect(fs_surface_class(FsStatus::parent_missing) == FsSurfaceClass::missing, "missing-parent");
    expect(fs_surface_class(FsStatus::already_exists) == FsSurfaceClass::conflict, "conflict-exists");
    expect(fs_surface_class(FsStatus::directory_not_empty) == FsSurfaceClass::conflict, "conflict-directory");
    expect(fs_surface_class(FsStatus::no_space) == FsSurfaceClass::capacity, "capacity");
    expect(fs_surface_class(FsStatus::permission_denied) == FsSurfaceClass::policy, "policy");
    expect(fs_surface_class(FsStatus::checksum_mismatch) == FsSurfaceClass::corruption, "corruption-checksum");
    expect(fs_surface_class(FsStatus::metadata_corrupt) == FsSurfaceClass::corruption, "corruption-metadata");
    expect(fs_surface_class(FsStatus::not_mounted) == FsSurfaceClass::unavailable, "unavailable");
    expect(fs_surface_class(FsStatus::io_error) == FsSurfaceClass::transport, "transport");
    expect(std::strcmp(fs_surface_class_name(FsSurfaceClass::policy), "policy") == 0, "class-name");
    expect(std::strcmp(fs_surface_message(FsStatus::not_found), "Path was not found.") == 0, "message-not-found");
    expect(std::strcmp(fs_surface_message(FsStatus::permission_denied), "Operation was denied by security policy.") == 0, "message-policy");
    expect(std::strcmp(fs_surface_message(FsStatus::io_error), "Block device I/O failed.") == 0, "message-io");

    if (failures != 0) return 1;
    std::printf("ZENOVFS_SURFACE_TEST_OK cases=%d classes=10 status=distinct\n", cases);
    return 0;
}
