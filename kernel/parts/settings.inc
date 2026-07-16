namespace settings {
char active_theme[16] = "midnight";
bool loaded_from_disk = false;

bool line_value(const char* text, uint32_t size, const char* key, char* output, uint32_t capacity) {
    const uint32_t key_length = static_cast<uint32_t>(string_length(key));
    for (uint32_t position = 0; position < size;) {
        const uint32_t line_start = position;
        while (position < size && text[position] != '\n' && text[position] != '\r') ++position;
        const uint32_t line_end = position;
        while (position < size && (text[position] == '\n' || text[position] == '\r')) ++position;
        if (line_end - line_start <= key_length || line_start + key_length >= size) continue;
        bool match = true;
        for (uint32_t i = 0; i < key_length; ++i) if (text[line_start + i] != key[i]) { match = false; break; }
        if (!match || text[line_start + key_length] != '=') continue;
        uint32_t value_length = line_end - line_start - key_length - 1U;
        if (value_length == 0 || value_length >= capacity) return false;
        memcpy(output, text + line_start + key_length + 1U, value_length);
        output[value_length] = 0;
        return true;
    }
    return false;
}

void init() {
    loaded_from_disk = false;
    string_copy(active_theme, "midnight", sizeof(active_theme));
    if (!storage::ready()) {
        serial::line("CONFIG_DEFAULT storage-offline");
        return;
    }

    uint8_t buffer[512];
    uint32_t size = 0;
    if (!storage::read_file("/data/config/system.ini", buffer, sizeof(buffer) - 1U, size)) {
        serial::line("CONFIG_DEFAULT missing-system.ini");
        return;
    }
    buffer[size] = 0;
    char requested_theme[16];
    if (!line_value(reinterpret_cast<const char*>(buffer), size, "theme", requested_theme, sizeof(requested_theme)) ||
        !console::select_theme(requested_theme)) {
        serial::line("CONFIG_DEFAULT invalid-theme");
        return;
    }
    string_copy(active_theme, requested_theme, sizeof(active_theme));
    loaded_from_disk = true;
    serial::write("CONFIG_LOAD_OK theme="); serial::line(active_theme);
}
}
