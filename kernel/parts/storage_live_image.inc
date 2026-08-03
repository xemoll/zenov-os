constexpr uint16_t live_overlay_empty = 0xFFFFU;
constexpr uint32_t live_overlay_capacity_sectors = 1024U;
constexpr uint32_t live_pack_header_bytes = 20U;
constexpr uint32_t live_pack_chunk_bytes = 8U;
static_assert(live_image_pack_bytes < 32768U,
              "embedded Live payload exceeds the low-memory budget");

uint16_t* live_overlay_map = nullptr;
uint8_t* live_overlay_sectors = nullptr;
uint32_t live_overlay_used = 0U;
bool live_storage_active = false;

uint16_t live_read_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t live_read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

const uint8_t* live_chunk_row(uint32_t index) {
    return live_image_pack + live_pack_header_bytes + index * live_pack_chunk_bytes;
}

uint32_t live_chunk_length(uint32_t index) {
    return live_read_u16(live_chunk_row(index) + 4U);
}

uint32_t live_pack_data_start() {
    return live_pack_header_bytes + live_image_chunk_count * live_pack_chunk_bytes;
}

uint32_t live_chunk_data_offset(uint32_t index) {
    uint32_t offset = 0U;
    for (uint32_t i = 0U; i < index; ++i) offset += live_chunk_length(i);
    return offset;
}

bool live_pack_valid() {
    if (live_image_pack_bytes < live_pack_header_bytes) return false;
    const char magic[8] = {'Z','L','I','V','E','0','0','2'};
    if (!bytes_equal(live_image_pack, reinterpret_cast<const uint8_t*>(magic), 8U)) return false;
    const uint32_t sectors = live_read_u32(live_image_pack + 8U);
    const uint32_t chunks = live_read_u32(live_image_pack + 12U);
    const uint32_t data_bytes = live_read_u32(live_image_pack + 16U);
    if (sectors != live_image_logical_sectors || chunks != live_image_chunk_count ||
        !sectors || !chunks) return false;
    const uint64_t table_end = static_cast<uint64_t>(live_pack_header_bytes) +
        static_cast<uint64_t>(chunks) * live_pack_chunk_bytes;
    if (table_end > live_image_pack_bytes || table_end + data_bytes != live_image_pack_bytes) {
        return false;
    }

    uint32_t previous_end = 0U;
    uint32_t accumulated_data = 0U;
    for (uint32_t i = 0U; i < chunks; ++i) {
        const uint8_t* row = live_chunk_row(i);
        const uint32_t offset = live_read_u32(row);
        const uint32_t length = live_read_u16(row + 4U);
        const uint32_t reserved = live_read_u16(row + 6U);
        if (!length || reserved != 0U || offset < previous_end ||
            static_cast<uint64_t>(offset) + length > static_cast<uint64_t>(sectors) * sector_size ||
            static_cast<uint64_t>(accumulated_data) + length > data_bytes) {
            return false;
        }
        previous_end = offset + length;
        accumulated_data += length;
    }
    return accumulated_data == data_bytes;
}

void live_read_base_sector(uint32_t lba, uint8_t* output) {
    memset(output, 0, sector_size);
    const uint32_t sector_begin = lba * sector_size;
    const uint32_t sector_end = sector_begin + sector_size;
    uint32_t low = 0U;
    uint32_t high = live_image_chunk_count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2U;
        const uint8_t* row = live_chunk_row(middle);
        const uint32_t end = live_read_u32(row) + live_read_u16(row + 4U);
        if (end <= sector_begin) low = middle + 1U;
        else high = middle;
    }

    uint32_t data_offset = live_chunk_data_offset(low);
    const uint32_t data_start = live_pack_data_start();
    for (uint32_t i = low; i < live_image_chunk_count; ++i) {
        const uint8_t* row = live_chunk_row(i);
        const uint32_t offset = live_read_u32(row);
        const uint32_t length = live_read_u16(row + 4U);
        if (offset >= sector_end) break;
        const uint32_t copy_begin = offset > sector_begin ? offset : sector_begin;
        const uint32_t chunk_end = offset + length;
        const uint32_t copy_end = chunk_end < sector_end ? chunk_end : sector_end;
        if (copy_end > copy_begin) {
            memcpy(output + copy_begin - sector_begin,
                   live_image_pack + data_start + data_offset + copy_begin - offset,
                   copy_end - copy_begin);
        }
        data_offset += length;
    }
}

BlockResult live_read_sector(uint32_t lba, uint8_t* output) {
    if (!live_storage_active || !output) {
        return block_result(BlockOperation::read, BlockStatus::no_device, lba, 0U, 0U, 0U);
    }
    if (lba >= live_image_logical_sectors) {
        return block_result(BlockOperation::read, BlockStatus::out_of_range, lba, 0U, 0U, 0U);
    }
    const uint16_t overlay = live_overlay_map[lba];
    if (overlay != live_overlay_empty) {
        memcpy(output, live_overlay_sectors + static_cast<uint32_t>(overlay) * sector_size,
               sector_size);
    } else {
        live_read_base_sector(lba, output);
    }
    return block_success(BlockOperation::read, lba);
}

BlockResult live_write_sector(uint32_t lba, const uint8_t* input) {
    if (!live_storage_active || !input) {
        return block_result(BlockOperation::write, BlockStatus::no_device, lba, 0U, 0U, 0U);
    }
    if (lba >= live_image_logical_sectors) {
        return block_result(BlockOperation::write, BlockStatus::out_of_range, lba, 0U, 0U, 0U);
    }
    uint16_t overlay = live_overlay_map[lba];
    if (overlay == live_overlay_empty) {
        if (live_overlay_used >= live_overlay_capacity_sectors) {
            return block_result(BlockOperation::write, BlockStatus::device_error, lba, 1U, 0U, 0U);
        }
        overlay = static_cast<uint16_t>(live_overlay_used++);
        live_overlay_map[lba] = overlay;
    }
    memcpy(live_overlay_sectors + static_cast<uint32_t>(overlay) * sector_size,
           input, sector_size);
    return block_success(BlockOperation::write, lba);
}

bool live_image_initialize() {
    live_storage_active = false;
    live_overlay_used = 0U;
    live_overlay_map = static_cast<uint16_t*>(
        heap::allocate(live_image_logical_sectors * sizeof(uint16_t), 16U));
    live_overlay_sectors = static_cast<uint8_t*>(
        heap::allocate(live_overlay_capacity_sectors * sector_size, 16U));
    if (!live_overlay_map || !live_overlay_sectors) {
        serial::line("ZENOVFS_LIVE_ALLOC_FAILED");
        return false;
    }
    if (!live_pack_valid()) {
        serial::line("ZENOVFS_LIVE_IMAGE_INVALID");
        return false;
    }
    memset(live_overlay_map, 0xFFU,
           live_image_logical_sectors * sizeof(uint16_t));
    memset(live_overlay_sectors, 0,
           live_overlay_capacity_sectors * sector_size);
    ata_present = false;
    ata_sector_total = live_image_logical_sectors;
    ata0 = BlockDevice{"live0", true, true, live_image_logical_sectors,
                       live_read_sector, live_write_sector};
    zfs_io_state = block_io_state_initial();
    live_storage_active = true;
    serial::line("ZENOVFS_LIVE_IMAGE_OK source=embedded format=ZLIVE002");
    serial::line("RAM_BLOCK_DEVICE_READY sectors=32768 overlay=1024");
    return true;
}
