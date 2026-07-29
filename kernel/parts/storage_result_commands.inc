namespace storage {

char* fs_skip_spaces(char* text) {
    while (text && is_space(*text)) ++text;
    return text;
}

bool fs_command_take(char* line, const char* command, char*& arguments) {
    if (!line || !command) return false;
    char* cursor = fs_skip_spaces(line);
    const char* expected = command;
    while (*expected && *cursor && storage_ascii_equal_ci(*cursor, *expected)) {
        ++cursor;
        ++expected;
    }
    if (*expected || (*cursor && !is_space(*cursor))) return false;
    arguments = fs_skip_spaces(cursor);
    return true;
}

bool fs_split_argument(char* arguments, char*& first, char*& remainder) {
    arguments = fs_skip_spaces(arguments);
    if (!arguments || !*arguments) return false;
    first = arguments;
    while (*arguments && !is_space(*arguments)) ++arguments;
    if (*arguments) *arguments++ = 0;
    remainder = fs_skip_spaces(arguments);
    return true;
}

bool fs_split_pair(char* arguments, char*& first, char*& second) {
    char* remainder = nullptr;
    if (!fs_split_argument(arguments, first, remainder) || !remainder || !*remainder) return false;
    second = remainder;
    while (*second && !is_space(*second)) ++second;
    if (*second) *second = 0;
    second = remainder;
    return *second != 0;
}

void fs_serial_result(const char* prefix, const char* action, const FsResult& result) {
    serial::write(prefix);
    serial::write(" action=");
    serial::write(action);
    serial::write(" status=");
    serial::write(fs_status_name(result.status));
    serial::write(" class=");
    serial::write(fs_surface_class_name(fs_surface_class(result.status)));
    if (result.has_block_error()) {
        serial::write(" block=");
        serial::write(block_status_name(result.block.status));
        serial::write(" lba=");
        ata2_serial_uint(result.block.lba);
        serial::write(" attempts=");
        ata2_serial_uint(result.block.attempts);
    }
    serial::write("\r\n");
}

void fs_print_failure(const char* action, const FsResult& result) {
    console::write("FS_COMMAND_FAILED action=");
    console::write(action);
    console::write(" status=");
    console::write(fs_status_name(result.status));
    console::write(" class=");
    console::line(fs_surface_class_name(fs_surface_class(result.status)));
    if (result.has_block_error()) {
        console::write("  Block         ");
        console::write(block_operation_name(result.block.operation));
        console::put('/');
        console::write(block_status_name(result.block.status));
        console::write(" lba=");
        console::unsigned_dec(result.block.lba);
        console::write(" attempts=");
        console::unsigned_dec(result.block.attempts);
        console::put('\n');
    }
    console::warning(fs_surface_message(result.status));
    fs_serial_result("FS_COMMAND_FAILED", action, result);
}

bool fs_finish_command(const char* action, const char* success_marker, const FsResult& raw) {
    const FsResult result = fs_record_result(raw);
    if (!result.ok()) {
        fs_print_failure(action, result);
        return false;
    }
    if (!result.durable()) {
        console::write("FS_COMMAND_RECOVERY_PENDING action=");
        console::line(action);
        fs_serial_result("FS_COMMAND_RECOVERY_PENDING", action, result);
        return true;
    }
    console::success(success_marker);
    serial::write(success_marker);
    serial::write("\r\n");
    return true;
}

bool dispatch_storage_result_command(char* line) {
    char* arguments = nullptr;

    if (fs_command_take(line, "cat", arguments)) {
        if (!arguments || !*arguments) {
            fs_print_failure("cat", fs_failure(FsOperation::read, FsStatus::invalid_argument));
            return true;
        }
        uint32_t size = 0U;
        const FsResult result = fs_record_result(
            guarded_read_file_result(arguments, file_buffer, max_file_bytes(), size));
        if (!result.ok()) {
            fs_print_failure("cat", result);
            return true;
        }
        for (uint32_t i = 0U; i < size; ++i) console::put(static_cast<char>(file_buffer[i]));
        if (!size || file_buffer[size - 1U] != '\n') console::put('\n');
        serial::line("CAT_OK");
        return true;
    }

    if (fs_command_take(line, "cd", arguments)) {
        if (!arguments || !*arguments) {
            print_working_directory();
            return true;
        }
        fs_finish_command("cd", "CD_OK", change_directory_result(arguments));
        return true;
    }

    if (fs_command_take(line, "mkdir", arguments)) {
        fs_finish_command("mkdir", "MKDIR_OK",
                          arguments && *arguments
                              ? make_directory_result(arguments)
                              : fs_failure(FsOperation::create, FsStatus::invalid_argument));
        return true;
    }

    if (fs_command_take(line, "touch", arguments)) {
        fs_finish_command("touch", "TOUCH_OK",
                          arguments && *arguments
                              ? touch_result(arguments)
                              : fs_failure(FsOperation::create, FsStatus::invalid_argument));
        return true;
    }

    bool append_command = false;
    if (fs_command_take(line, "write", arguments) ||
        ((append_command = fs_command_take(line, "append", arguments)))) {
        char* path = nullptr;
        char* text = nullptr;
        if (!fs_split_argument(arguments, path, text) || !text || !*text) {
            fs_finish_command(append_command ? "append" : "write",
                              append_command ? "APPEND_OK" : "WRITE_OK",
                              fs_failure(FsOperation::write, FsStatus::invalid_argument));
            return true;
        }
        fs_finish_command(append_command ? "append" : "write",
                          append_command ? "APPEND_OK" : "WRITE_OK",
                          guarded_write_file_result(
                              path, reinterpret_cast<const uint8_t*>(text),
                              static_cast<uint32_t>(string_length(text)), append_command));
        return true;
    }

    if (fs_command_take(line, "rm", arguments) || fs_command_take(line, "del", arguments)) {
        fs_finish_command("remove", "REMOVE_OK",
                          arguments && *arguments
                              ? guarded_remove_result(arguments)
                              : fs_failure(FsOperation::remove, FsStatus::invalid_argument));
        return true;
    }

    if (fs_command_take(line, "cp", arguments) || fs_command_take(line, "copy", arguments)) {
        char* source = nullptr;
        char* destination = nullptr;
        const FsResult result =
            fs_split_pair(arguments, source, destination)
                ? guarded_copy_file_result(source, destination)
                : fs_failure(FsOperation::copy, FsStatus::invalid_argument);
        fs_finish_command("copy", "COPY_OK", result);
        return true;
    }

    if (fs_command_take(line, "mv", arguments) || fs_command_take(line, "ren", arguments)) {
        char* source = nullptr;
        char* destination = nullptr;
        const FsResult result =
            fs_split_pair(arguments, source, destination)
                ? guarded_rename_entry_result(source, destination)
                : fs_failure(FsOperation::rename, FsStatus::invalid_argument);
        fs_finish_command("move", "MOVE_OK", result);
        return true;
    }

    if (fs_command_take(line, "sync", arguments)) {
        (void)arguments;
        fs_finish_command("sync", "SYNC_OK", sync_metadata_result());
        return true;
    }

    if (fs_command_take(line, "fsck", arguments)) {
        (void)arguments;
        const FsResult result = fs_record_result(check_integrity_result(true));
        if (!result.ok()) fs_print_failure("fsck", result);
        return true;
    }

    if (fs_command_take(line, "stat", arguments)) {
        FileInfo info{};
        const FsResult result = fs_record_result(
            arguments && *arguments
                ? query_result(arguments, info)
                : fs_failure(FsOperation::query, FsStatus::invalid_argument));
        if (!result.ok()) {
            fs_print_failure("stat", result);
            return true;
        }
        console::write("  Type          ");
        console::line(info.type == zfs_type_directory ? "directory" : "file");
        if (info.type == zfs_type_file) {
            console::write("  Size          ");
            console::unsigned_dec(info.size);
            console::line(" bytes");
            console::write("  Checksum      ");
            console::hex(info.checksum);
            console::put('\n');
        }
        serial::line("STAT_OK");
        return true;
    }

    return false;
}

} // namespace storage
