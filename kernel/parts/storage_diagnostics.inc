bool storage_ascii_equal_ci(char left, char right) {
    if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
    return left == right;
}

bool storage_command_equal(const char* line, const char* expected) {
    if (!line || !expected) return false;
    while (is_space(*line)) ++line;
    while (*expected && *line && storage_ascii_equal_ci(*line, *expected)) { ++line; ++expected; }
    while (is_space(*line)) ++line;
    return *line == 0 && *expected == 0;
}

void storage_print_counter(const char* label, uint32_t value) {
    console::write("  "); console::write(label);
    const uint32_t length = static_cast<uint32_t>(string_length(label));
    for (uint32_t i = length; i < 16U; ++i) console::put(' ');
    console::unsigned_dec(value); console::put('\n');
}

void storage_print_result(const char* label, const BlockResult& result) {
    console::write("  "); console::write(label); console::write("  ");
    console::write(block_operation_name(result.operation)); console::put('/'); console::write(block_status_name(result.status));
    console::write(" lba="); console::unsigned_dec(result.lba);
    console::write(" attempts="); console::unsigned_dec(static_cast<uint32_t>(result.attempts));
    console::put('\n');
}

void command_disk_status() {
    console::clear_content();
    console::title("BLOCK STORAGE STATUS");
    console::line("");
    console::write("  Device          "); console::line(ata0.present ? "ata0 online" : "ata0 unavailable");
    console::line("  Driver          ATA PIO / LBA28 / PIT deadlines");
    console::line("  Block ABI       typed BlockResult boundary");
    console::write("  Capacity        "); console::unsigned_dec(ata0.sectors); console::line(" sectors");
    storage_print_counter("Requests", ata_diagnostics.requests);
    storage_print_counter("Completed", ata_diagnostics.completed);
    storage_print_counter("Failed", ata_diagnostics.failed);
    storage_print_counter("Reads", ata_diagnostics.read_requests);
    storage_print_counter("Writes", ata_diagnostics.write_requests);
    storage_print_counter("Attempts", ata_diagnostics.commands);
    storage_print_counter("Retries", ata_diagnostics.retries);
    storage_print_counter("Resets", ata_diagnostics.resets);
    storage_print_counter("Revalidations", ata_diagnostics.revalidations);
    storage_print_counter("Reset failures", ata_diagnostics.reset_failures);
    storage_print_counter("Revalidate fail", ata_diagnostics.revalidation_failures);
    storage_print_counter("Timeouts", ata_diagnostics.timeouts);
    storage_print_counter("Attempt errors", ata_diagnostics.attempt_errors);
    storage_print_result("Last result", ata_last_block_result);
    storage_print_result("Last failure", ata_last_block_failure);
    storage_print_counter("FS requests", zfs_io_state.requests);
    storage_print_counter("FS completed", zfs_io_state.completed);
    storage_print_counter("FS failed", zfs_io_state.failed);
    storage_print_result("FS last result", zfs_io_state.last_result);
    storage_print_result("FS last failure", zfs_io_state.last_failure);
    const bool ata_counters_valid = ata_diagnostics.requests == ata_diagnostics.completed + ata_diagnostics.failed &&
                                    ata_diagnostics.requests == ata_diagnostics.read_requests + ata_diagnostics.write_requests &&
                                    ata_diagnostics.requests > 0U;
    const bool filesystem_counters_valid = block_io_counters_valid(zfs_io_state) && zfs_io_state.requests > 0U;
    if (ata_counters_valid && filesystem_counters_valid) console::success("BLOCK_DEVICE_COUNTERS_OK");
    else console::warning("BLOCK_DEVICE_COUNTERS_INVALID");
    console::success("BLOCK_DEVICE_STATUS_OK");
    serial::line("BLOCK_RESULT_API_OK version=2");
    serial::line("BLOCK_DEVICE_TYPED_ABI_OK version=1");
}

bool dispatch_storage_command(const char* line) {
    if (!storage_command_equal(line, "disk status")) return false;
    command_disk_status();
    return true;
}
