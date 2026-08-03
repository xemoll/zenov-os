namespace storage {
#include "storage_block_result.inc"
#include "storage_block_device.inc"
#include "storage_block_device_state.inc"
#include "generated/live_zenovfs.hpp"

bool bytes_equal(const uint8_t* left, const uint8_t* right, uint32_t count) {
    if (!left || !right) return false;
    for (uint32_t i = 0U; i < count; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

#include "storage_live_image.inc"
#include "storage_fs_result.inc"
#include "storage_fs_surface.inc"
#include "storage_fs_state.inc"
#include "storage_zenovfs_core.inc"
#include "storage_zenovfs_io.inc"
#include "storage_zenovfs_commands.inc"
} // namespace storage
