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
    for (uint32_t i = length; i < 20U; ++i) console::put(' ');
    console::unsigned_dec(value); console::put('\n');
}

void storage_print_result(const char* label, const BlockResult& result) {
    console::write("  "); console::write(label); console::write("  ");
    console::write(block_operation_name(result.operation)); console::put('/'); console::write(block_status_name(result.status));
    console::write(" lba="); console::unsigned_dec(result.lba);
    console::write(" attempts="); console::unsigned_dec(static_cast<uint32_t>(result.attempts));
    console::put('\n');
}

void storage_print_fs_result(const char* label, const FsResult& result) {
    console::write("  "); console::write(label); console::write("  ");
    console::write(fs_operation_name(result.operation)); console::put('/');
    console::write(fs_status_name(result.status));
    console::write(" class="); console::write(fs_surface_class_name(fs_surface_class(result.status)));
    console::write(" bytes="); console::unsigned_dec(result.bytes);
    if (result.has_block_error()) {
        console::write(" block=");
        console::write(block_status_name(result.block.status));
        console::write(" lba="); console::unsigned_dec(result.block.lba);
    }
    console::put('\n');
}

void command_disk_status() {
    console::clear_content();
    console::title("BLOCK STORAGE STATUS");
    console::line("");
    console::write("  Device              "); console::line(ata0.present ? "ata0 online" : "ata0 unavailable");
    console::write("  Filesystem          "); console::line(zfs_mounted ? "ZenovFS1 mounted" : "ZenovFS1 unavailable");
    console::line("  Driver              ATA PIO / LBA28 / PIT deadlines");
    console::line("  Block ABI           typed BlockResult boundary");
    console::line("  Filesystem ABI      typed FsResult boundary");
    console::line("  Result surface      distinct shell/syscall/security status");
    console::write("  Capacity            "); console::unsigned_dec(ata0.sectors); console::line(" sectors");

    storage_print_counter("ATA requests", ata_diagnostics.requests);
    storage_print_counter("ATA completed", ata_diagnostics.completed);
    storage_print_counter("ATA failed", ata_diagnostics.failed);
    storage_print_counter("ATA reads", ata_diagnostics.read_requests);
    storage_print_counter("ATA writes", ata_diagnostics.write_requests);
    storage_print_counter("ATA attempts", ata_diagnostics.commands);
    storage_print_counter("ATA retries", ata_diagnostics.retries);
    storage_print_counter("ATA resets", ata_diagnostics.resets);
    storage_print_counter("ATA revalidations", ata_diagnostics.revalidations);
    storage_print_counter("ATA reset failures", ata_diagnostics.reset_failures);
    storage_print_counter("ATA revalidate fail", ata_diagnostics.revalidation_failures);
    storage_print_counter("ATA timeouts", ata_diagnostics.timeouts);
    storage_print_counter("ATA attempt errors", ata_diagnostics.attempt_errors);
    storage_print_result("ATA last result", ata_last_block_result);
    storage_print_result("ATA last failure", ata_last_block_failure);

    storage_print_counter("Sector requests", zfs_io_state.requests);
    storage_print_counter("Sector completed", zfs_io_state.completed);
    storage_print_counter("Sector failed", zfs_io_state.failed);
    storage_print_result("Sector last result", zfs_io_state.last_result);
    storage_print_result("Sector last failure", zfs_io_state.last_failure);

    storage_print_counter("FS API requests", zfs_result_state.requests);
    storage_print_counter("FS API completed", zfs_result_state.completed);
    storage_print_counter("FS API failed", zfs_result_state.failed);
    storage_print_counter("FS recovery pending", zfs_result_state.recovery_pending);
    storage_print_fs_result("FS API last result", zfs_result_state.last_result);
    storage_print_fs_result("FS API last failure", zfs_result_state.last_failure);

    const bool ata_counters_valid =
        ata_diagnostics.requests == ata_diagnostics.completed + ata_diagnostics.failed &&
        ata_diagnostics.requests == ata_diagnostics.read_requests + ata_diagnostics.write_requests &&
        ata_diagnostics.requests > 0U;
    const bool sector_counters_valid = block_io_counters_valid(zfs_io_state) && zfs_io_state.requests > 0U;
    const bool filesystem_results_valid = fs_result_counters_valid(zfs_result_state) &&
        zfs_result_state.requests > 0U;
    if (ata_counters_valid && sector_counters_valid) console::success("BLOCK_DEVICE_COUNTERS_OK");
    else console::warning("BLOCK_DEVICE_COUNTERS_INVALID");
    if (filesystem_results_valid) console::success("ZENOVFS_RESULT_COUNTERS_OK");
    else console::warning("ZENOVFS_RESULT_COUNTERS_INVALID");
    console::success("BLOCK_DEVICE_STATUS_OK");
    serial::line("BLOCK_RESULT_API_OK version=2");
    serial::line("BLOCK_DEVICE_TYPED_ABI_OK version=1");
    serial::line("ZENOVFS_RESULT_API_OK version=1");
    serial::line("ZENOVFS_SURFACE_API_OK version=1");
}

bool dispatch_storage_result_command(char* line);

bool dispatch_storage_command(char* line) {
    if (storage_command_equal(line, "disk status")) {
        command_disk_status();
        return true;
    }
    return dispatch_storage_result_command(line);
}
