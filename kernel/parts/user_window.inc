namespace paging {
uint32_t process_window_scrub_count = 0;

bool scrub_process_window(bool verify) {
    clear_user();
    if (!map_user(0, process_window_bytes, true)) return false;

    auto* const window = reinterpret_cast<uint8_t*>(user_virtual_base);
    if (verify) {
        window[0] = 0xA5U;
        window[page_size - 1U] = 0x5AU;
        window[process_window_bytes - 1U] = 0xC3U;
    }

    memset(window, 0, process_window_bytes);
    const bool clean = window[0] == 0 &&
                       window[page_size - 1U] == 0 &&
                       window[process_window_bytes - 1U] == 0;
    clear_user();
    return clean;
}
} // namespace paging
