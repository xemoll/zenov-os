ata_policy::Result ata2_identify_once(uint8_t attempts) {
    out8(ata2_control, ata2_control_nien);
    out8(ata2_drive, 0xE0U);
    io_wait();
    out8(ata2_sector_count, 0U);
    out8(ata2_lba_low, 0U);
    out8(ata2_lba_mid, 0U);
    out8(ata2_lba_high, 0U);
    out8(ata2_command, ata2_command_identify);

    const uint8_t initial = in8(ata2_status);
    if (initial == 0U || initial == 0xFFU) return ata2_result(ata_policy::Error::no_device, initial, 0U, attempts);
    ata_policy::Result result = ata2_wait(true, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return result;
    if (in8(ata2_lba_mid) != 0U || in8(ata2_lba_high) != 0U) {
        return ata2_result(ata_policy::Error::no_device, result.status, 0U, attempts);
    }

    uint16_t identify[256];
    for (uint32_t i = 0; i < 256U; ++i) identify[i] = ata2_in16(ata2_data);
    result = ata2_wait(false, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return result;

    ata_sector_total = static_cast<uint32_t>(identify[60]) | (static_cast<uint32_t>(identify[61]) << 16U);
    if (ata_sector_total == 0U) return ata2_result(ata_policy::Error::no_device, result.status, 0U, attempts);
    return result;
}

bool ata2_identify() {
    for (uint8_t attempt = 1U; attempt <= ata2_max_attempts; ++attempt) {
        ++ata_diagnostics.commands;
        const ata_policy::Result result = ata2_identify_once(attempt);
        if (result.ok()) {
            ata_present = true;
            ata2_record_success(result);
            serial::line("ATA_IDENTIFY_OK deadline=3000ms retry-limit=1");
            return true;
        }
        ata2_record_failure("identify", result);
        if (attempt == ata2_max_attempts || !ata_policy::retryable(result.error)) return false;
        ++ata_diagnostics.retries;
        if (!ata2_soft_reset(attempt).ok()) return false;
    }
    return false;
}
