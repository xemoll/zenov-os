constexpr uint32_t sector_size = 512U;

bool ata_present = false;
uint32_t ata_sector_total = 0U;
BlockDevice ata0{"ata0", false, true, 0U, nullptr, nullptr};
BlockIoState zfs_io_state = block_io_state_initial();

inline BlockResult zfs_read_sector(uint32_t lba, uint8_t* output) {
    return block_device_read(ata0, zfs_io_state, lba, output);
}

inline BlockResult zfs_write_sector(uint32_t lba, const uint8_t* input) {
    return block_device_write(ata0, zfs_io_state, lba, input);
}
