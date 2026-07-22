AtaOperationResult ata2_read_once(uint32_t lba, uint8_t* output, uint8_t attempts) {
    ata_policy::Result result = ata2_wait(false, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return AtaOperationResult{result, BlockOperation::read};
    ata2_select(lba);
    ata2_program_lba28(lba);
    out8(ata2_command, ata2_command_read_sectors);
    result = ata2_wait(true, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return AtaOperationResult{result, BlockOperation::read};
    for (uint32_t i = 0; i < 256U; ++i) {
        const uint16_t value = ata2_in16(ata2_data);
        output[i * 2U] = static_cast<uint8_t>(value);
        output[i * 2U + 1U] = static_cast<uint8_t>(value >> 8U);
    }
    return AtaOperationResult{ata2_wait(false, ata2_command_timeout_ms, attempts), BlockOperation::read};
}

BlockResult ata2_read_result(uint32_t lba, uint8_t* output) {
    ata2_begin_request(BlockOperation::read);
    if (!output) {
        const ata_policy::Result error = ata2_result(ata_policy::Error::invalid_argument, 0U, 0U, 0U);
        ata2_record_attempt_failure("read", BlockOperation::read, lba, error);
        return ata2_complete_request(ata2_block_result(BlockOperation::read, lba, error));
    }
    if (!ata_present) {
        const ata_policy::Result error = ata2_result(ata_policy::Error::no_device, 0U, 0U, 0U);
        ata2_record_attempt_failure("read", BlockOperation::read, lba, error);
        return ata2_complete_request(ata2_block_result(BlockOperation::read, lba, error));
    }
    if (lba >= ata_sector_total) {
        const ata_policy::Result error = ata2_result(ata_policy::Error::out_of_range, 0U, 0U, 0U);
        ata2_record_attempt_failure("read", BlockOperation::read, lba, error);
        return ata2_complete_request(ata2_block_result(BlockOperation::read, lba, error));
    }
    for (uint8_t attempt = 1U; attempt <= ata2_max_attempts; ++attempt) {
        ++ata_diagnostics.commands;
        const AtaOperationResult result = ata2_read_once(lba, output, attempt);
        if (result.transport.ok()) {
            const BlockResult success = ata2_block_result(BlockOperation::read, lba, result.transport);
            if (attempt > 1U) serial::line("ATA_RECOVERY_RETRY_OK op=read attempts=2");
            return ata2_complete_request(success);
        }
        ata2_record_attempt_failure("read", result.operation, lba, result.transport);
        const BlockResult failure = ata2_block_result(result.operation, lba, result.transport);
        if (attempt == ata2_max_attempts || !block_retryable(failure.status)) return ata2_complete_request(failure);
        ++ata_diagnostics.retries;
        if (!ata2_soft_reset(attempt).ok()) {
            return ata2_complete_request(ata2_block_result(BlockOperation::reset, lba, ata_last_result));
        }
    }
    return ata2_complete_request(block_result(BlockOperation::read, BlockStatus::device_error, lba, 0U, 0U, 0U));
}

AtaOperationResult ata2_write_once(uint32_t lba, const uint8_t* input, uint8_t attempts) {
    ata_policy::Result result = ata2_wait(false, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return AtaOperationResult{result, BlockOperation::write};
    ata2_select(lba);
    ata2_program_lba28(lba);
    out8(ata2_command, ata2_command_write_sectors);
    result = ata2_wait(true, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return AtaOperationResult{result, BlockOperation::write};
    for (uint32_t i = 0; i < 256U; ++i) {
        const uint16_t value = static_cast<uint16_t>(input[i * 2U]) |
                               static_cast<uint16_t>(static_cast<uint16_t>(input[i * 2U + 1U]) << 8U);
        ata2_out16(ata2_data, value);
    }
    result = ata2_wait(false, ata2_command_timeout_ms, attempts);
    if (!result.ok()) return AtaOperationResult{result, BlockOperation::write};
    out8(ata2_command, ata2_command_flush_cache);
    return AtaOperationResult{ata2_wait(false, ata2_command_timeout_ms, attempts), BlockOperation::flush};
}

BlockResult ata2_write_result(uint32_t lba, const uint8_t* input) {
    ata2_begin_request(BlockOperation::write);
    if (!input) {
        const ata_policy::Result error = ata2_result(ata_policy::Error::invalid_argument, 0U, 0U, 0U);
        ata2_record_attempt_failure("write", BlockOperation::write, lba, error);
        return ata2_complete_request(ata2_block_result(BlockOperation::write, lba, error));
    }
    if (!ata_present) {
        const ata_policy::Result error = ata2_result(ata_policy::Error::no_device, 0U, 0U, 0U);
        ata2_record_attempt_failure("write", BlockOperation::write, lba, error);
        return ata2_complete_request(ata2_block_result(BlockOperation::write, lba, error));
    }
    if (lba >= ata_sector_total) {
        const ata_policy::Result error = ata2_result(ata_policy::Error::out_of_range, 0U, 0U, 0U);
        ata2_record_attempt_failure("write", BlockOperation::write, lba, error);
        return ata2_complete_request(ata2_block_result(BlockOperation::write, lba, error));
    }
    for (uint8_t attempt = 1U; attempt <= ata2_max_attempts; ++attempt) {
        ++ata_diagnostics.commands;
        const AtaOperationResult result = ata2_write_once(lba, input, attempt);
        if (result.transport.ok()) {
            const BlockResult success = ata2_block_result(BlockOperation::write, lba, result.transport);
            if (attempt > 1U) serial::line("ATA_RECOVERY_RETRY_OK op=write attempts=2");
            return ata2_complete_request(success);
        }
        ata2_record_attempt_failure("write", result.operation, lba, result.transport);
        const BlockResult failure = ata2_block_result(result.operation, lba, result.transport);
        if (attempt == ata2_max_attempts || !block_retryable(failure.status)) return ata2_complete_request(failure);
        ++ata_diagnostics.retries;
        if (!ata2_soft_reset(attempt).ok()) {
            return ata2_complete_request(ata2_block_result(BlockOperation::reset, lba, ata_last_result));
        }
    }
    return ata2_complete_request(block_result(BlockOperation::write, BlockStatus::device_error, lba, 0U, 0U, 0U));
}
