void init_deadline() {
    heap::init();
    serial::line("Storage: probing ATA primary master with monotonic deadlines...");
    ata_present = false;
    ata_sector_total = 0U;
    ata0.read_sector = ata2_read_sector;
    ata0.write_sector = ata2_write_sector;
    ata0.present = ata2_identify();
    ata0.sectors = ata_sector_total;
    if (!ata0.present) {
        serial::line("Storage: ATA device not found or recovery failed");
        return;
    }
    serial::line("ATA_DEADLINE_DRIVER_READY clock=pit100 timeout=3000ms reset=5000ms attempts=2");
    serial::line("Storage: ATA PIO device online");
    if (!mount()) {
        serial::line("Storage: ZenovFS mount failed");
        return;
    }
    serial::line("ZENOVFS_MOUNT_OK");
}
