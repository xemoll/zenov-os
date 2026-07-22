ata_policy::Result ata2_read_once(uint32_t lba, uint8_t* output, uint8_t attempts) {
    ata_policy::Result result = ata2_wait(false, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return result;
    ata2_select(lba);
    ata2_program_lba28(lba);
    out8(ata2_command, ata2_command_read_sectors);
    result = ata2_wait(true, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return result;
    for (uint32_t i = 0; i < 256U; ++i) {
        const uint16_t value = ata2_in16(ata2_data);
        output[i * 2U] = static_cast<uint8_t>(value);
        output[i * 2U + 1U] = static_cast<uint8_t>(value >> 8U);
    }
    return ata2_wait(false, ata2_command_timeout_ms, attempts);
}

bool ata2_read_sector(uint32_t lba, uint8_t* output) {
    if (!output) {
        ata2_record_failure("read", ata2_result(ata_policy::Error::invalid_argument, 0U, 0U, 0U));
        return false;
    }
    if (!ata_present) {
        ata2_record_failure("read", ata2_result(ata_policy::Error::no_device, 0U, 0U, 0U));
        return false;
    }
    if (lba >= ata_sector_total) {
        ata2_record_failure("read", ata2_result(ata_policy::Error::out_of_range, 0U, 0U, 0U));
        return false;
    }
    for (uint8_t attempt = 1U; attempt <= ata2_max_attempts; ++attempt) {
        ++ata_diagnostics.commands;
        const ata_policy::Result result = ata2_read_once(lba, output, attempt);
        if (result.ok()) { ata2_record_success(result); return true; }
        ata2_record_failure("read", result);
        if (attempt == ata2_max_attempts || !ata_policy::retryable(result.error)) return false;
        ++ata_diagnostics.retries;
        if (!ata2_soft_reset(attempt).ok()) return false;
    }
    return false;
}

ata_policy::Result ata2_write_once(uint32_t lba, const uint8_t* input, uint8_t attempts) {
    ata_policy::Result result = ata2_wait(false, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return result;
    ata2_select(lba);
    ata2_program_lba28(lba);
    out8(ata2_command, ata2_command_write_sectors);
    result = ata2_wait(true, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return result;
    for (uint32_t i = 0; i < 256U; ++i) {
        const uint16_t value = static_cast<uint16_t>(input[i * 2U]) |
                               static_cast<uint16_t>(static_cast<uint16_t>(input[i * 2U + 1U]) << 8U);
        ata2_out16(ata2_data, value);
    }
    out8(ata2_command, ata2_command_flush_cache);
    return ata2_wait(false, ata2_command_timeout_ms, attempts);
}

bool ata2_write_sector(uint32_t lba, const uint8_t* input) {
    if (!input) {
        ata2_record_failure("write", ata2_result(ata_policy::Error::invalid_argument, 0U, 0U, 0U));
        return false;
    }
    if (!ata_present) {
        ata2_record_failure("write", ata2_result(ata_policy::Error::no_device, 0U, 0U, 0U));
        return false;
    }
    if (lba >= ata_sector_total) {
        ata2_record_failure("write", ata2_result(ata_policy::Error::out_of_range, 0U, 0U, 0U));
        return false;
    }
    for (uint8_t attempt = 1U; attempt <= ata2_max_attempts; ++attempt) {
        ++ata_diagnostics.commands;
        const ata_policy::Result result = ata2_write_once(lba, input, attempt);
        if (result.ok()) { ata2_record_success(result); return true; }
        ata2_record_failure("write", result);
        if (attempt == ata2_max_attempts || !ata_policy::retryable(result.error)) return false;
        ++ata_diagnostics.retries;
        if (!ata2_soft_reset(attempt).ok()) return false;
    }
    return false;
}
