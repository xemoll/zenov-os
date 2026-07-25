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
    zfs_result_state = FsResultState{
        0U, 0U, 0U, 0U,
        fs_success(FsOperation::none),
        fs_success(FsOperation::none)
    };

    const bool identified = ata2_identify();
    ata2_publish_device_state(identified, identified ? ata_sector_total : 0U);
    if (!ata0.present) {
        serial::line("Storage: ATA device not found or recovery failed");
        return;
    }
    if (!block_device_contract_valid(ata0)) panic("Typed block device contract validation failed.");
    serial::line("BLOCK_DEVICE_TYPED_ABI_READY version=1");
    serial::line("ATA_DEADLINE_DRIVER_READY clock=pit100 timeout=3000ms reset=5000ms attempts=2");
    serial::line("Storage: ATA PIO device online");

    const FsResult mounted = fs_record_result(mount_result());
    if (!mounted.ok()) {
        serial::write("Storage: ZenovFS mount failed status=");
        serial::line(fs_status_name(mounted.status));
        return;
    }
    serial::line("ZENOVFS_RESULT_API_READY version=1");
    serial::line("ZENOVFS_MOUNT_OK");
}
