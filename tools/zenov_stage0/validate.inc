void signature(const Call& call, std::initializer_list<bool> strings) {
    if (call.is_string.size() != strings.size()) validation_error(call, "wrong argument count for " + call.name);
    std::size_t i = 0;
    for (bool want_string : strings) {
        if (call.is_string[i] != want_string) validation_error(call, "wrong argument type for " + call.name);
        ++i;
    }
}

bool valid_name(const std::string& value, std::size_t max_size) {
    if (value.empty() || value.size() > max_size) return false;
    for (char c : value) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

Config validate(const std::vector<Call>& calls) {
    constexpr std::size_t max_items = 256;
    constexpr std::size_t max_generated_text = 48U * 1024U;
    Config config;
    std::unordered_set<std::string> command_names;
    std::unordered_set<std::string> file_names;
    std::size_t generated_text = 0;
    for (const Call& call : calls) {
        if (call.name == "system_name") {
            signature(call, {true});
            if (!config.system_name.empty()) validation_error(call, "duplicate system_name");
            config.system_name = call.string_args[0];
        } else if (call.name == "system_version") {
            signature(call, {true});
            if (!config.version.empty()) validation_error(call, "duplicate system_version");
            config.version = call.string_args[0];
        } else if (call.name == "shell_prompt") {
            signature(call, {true});
            if (!config.prompt.empty()) validation_error(call, "duplicate shell_prompt");
            config.prompt = call.string_args[0];
        } else if (call.name == "theme") {
            signature(call, {false, false});
            const auto fg = call.number_args[0];
            const auto bg = call.number_args[1];
            if (fg < 0 || fg > 15 || bg < 0 || bg > 7) validation_error(call, "theme values out of range");
            config.foreground = static_cast<std::uint8_t>(fg);
            config.background = static_cast<std::uint8_t>(bg);
        } else if (call.name == "shell_limits") {
            signature(call, {false, false});
            const auto line_capacity = call.number_args[0];
            const auto history_capacity = call.number_args[1];
            if (line_capacity < 128 || line_capacity > 1024) validation_error(call, "shell line capacity must be 128..1024");
            if (history_capacity < 16 || history_capacity > 256) validation_error(call, "shell history capacity must be 16..256");
            if (line_capacity * history_capacity > 128 * 1024) validation_error(call, "shell history memory budget exceeds 128 KiB");
            config.shell_line_capacity = static_cast<std::uint32_t>(line_capacity);
            config.shell_history_capacity = static_cast<std::uint32_t>(history_capacity);
        } else if (call.name == "boot_message") {
            signature(call, {true});
            if (call.string_args[0].empty() || call.string_args[0].size() > 78) validation_error(call, "boot message length must be 1..78");
            if (config.boot_messages.size() >= max_items) validation_error(call, "too many boot messages");
            config.boot_messages.push_back(call.string_args[0]);
        } else if (call.name == "shell_command") {
            signature(call, {true, true});
            const auto& name = call.string_args[0];
            if (!valid_name(name, 31)) validation_error(call, "invalid command name");
            if (!command_names.insert(name).second) validation_error(call, "duplicate command " + name);
            if (call.string_args[1].size() > 4096) validation_error(call, "command response exceeds 4096 bytes");
            if (config.commands.size() >= max_items) validation_error(call, "too many static commands");
            generated_text += name.size() + call.string_args[1].size();
            config.commands.emplace_back(name, call.string_args[1]);
        } else if (call.name == "vfs_file") {
            signature(call, {true, true});
            const auto& name = call.string_args[0];
            if (!valid_name(name, 31)) validation_error(call, "invalid VFS file name");
            if (!file_names.insert(name).second) validation_error(call, "duplicate VFS file " + name);
            if (call.string_args[1].size() > 16384) validation_error(call, "VFS file content exceeds 16384 bytes");
            if (config.files.size() >= max_items) validation_error(call, "too many VFS files");
            generated_text += name.size() + call.string_args[1].size();
            config.files.emplace_back(name, call.string_args[1]);
        } else {
            validation_error(call, "unsupported Zenov stage0 call " + call.name);
        }
        if (generated_text > max_generated_text) validation_error(call, "generated text budget exceeds 48 KiB");
    }
    if (config.system_name != "ZenovOS") throw Error("system_name must remain ZenovOS");
    if (config.version != "0.1.1") throw Error("system_version must be 0.1.1 for this release");
    if (config.prompt.empty() || config.prompt.size() > 24) throw Error("shell_prompt length must be 1..24");
    if (config.boot_messages.empty()) throw Error("at least one boot_message is required");
    if (config.files.empty()) throw Error("at least one vfs_file is required");
    return config;
}

std::string cpp_escape(const std::string& input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            case '\0': out << "\\0"; break;
            default:
                if (c < 32 || c > 126) {
                    const char* hex = "0123456789ABCDEF";
                    out << "\\x" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string generate(const Config& config) {
    std::ostringstream out;
    out << "#pragma once\n\nnamespace zenov_generated {\n";
    out << "struct NamedText { const char* name; const char* text; };\n";
    out << "inline constexpr char kSystemName[] = \"" << cpp_escape(config.system_name) << "\";\n";
    out << "inline constexpr char kVersion[] = \"" << cpp_escape(config.version) << "\";\n";
    out << "inline constexpr char kPrompt[] = \"" << cpp_escape(config.prompt) << "\";\n";
    out << "inline constexpr unsigned char kForeground = " << static_cast<unsigned>(config.foreground) << ";\n";
    out << "inline constexpr unsigned char kBackground = " << static_cast<unsigned>(config.background) << ";\n";
    out << "inline constexpr unsigned int kShellLineCapacity = " << config.shell_line_capacity << ";\n";
    out << "inline constexpr unsigned int kShellHistoryCapacity = " << config.shell_history_capacity << ";\n";
    out << "inline constexpr const char* kBootMessages[] = {\n";
    for (const auto& value : config.boot_messages) out << "    \"" << cpp_escape(value) << "\",\n";
    out << "};\ninline constexpr unsigned int kBootMessageCount = sizeof(kBootMessages) / sizeof(kBootMessages[0]);\n";
    out << "inline constexpr NamedText kStaticCommands[] = {\n";
    for (const auto& item : config.commands) out << "    {\"" << cpp_escape(item.first) << "\", \"" << cpp_escape(item.second) << "\"},\n";
    out << "};\ninline constexpr unsigned int kStaticCommandCount = sizeof(kStaticCommands) / sizeof(kStaticCommands[0]);\n";
    out << "inline constexpr NamedText kFiles[] = {\n";
    for (const auto& item : config.files) out << "    {\"" << cpp_escape(item.first) << "\", \"" << cpp_escape(item.second) << "\"},\n";
    out << "};\ninline constexpr unsigned int kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);\n";
    out << "}\n";
    return out.str();
}

}
