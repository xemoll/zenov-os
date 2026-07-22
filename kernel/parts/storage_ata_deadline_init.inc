void init_deadline() {
    heap::init();
    serial::line("Storage: probing ATA primary master with monotonic deadlines...");
    ata_present = false;
    ata_sector_total = 0U;
    ata0.present = false;
    ata0.sectors = 0U;
    ata0.read_sector = ata2_read_result;
    ata0.write_sector = ata2_write_result;
    zfs_io_state = block_io_state_initial();

    const bool identified = ata2_identify();
    ata2_publish_device_state(identified, identified ? ata_sector_total : 0U);
    if (!ata0.present) {
        serial::line("Storage: ATA device not found or recovery failed");
        return;
    }
    serial::line("ATA_DEADLINE_DRIVER_READY clock=pit100 timeout=3000ms reset=5000ms attempts=2");
    serial::line("BLOCK_DEVICE_TYPED_ABI_READY version=1");
    serial::line("Storage: ATA PIO device online");
    if (!mount()) {
        serial::line("Storage: ZenovFS mount failed");
        return;
    }
    serial::line("ZENOVFS_MOUNT_OK");
}
