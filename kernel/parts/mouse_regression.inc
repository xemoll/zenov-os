bool mouse_irq_route_ready() {
    const uintptr_t configured = static_cast<uintptr_t>(idt[44].low) |
        (static_cast<uintptr_t>(idt[44].high) << 16U);
    const bool idt_ready = configured == reinterpret_cast<uintptr_t>(isr44) &&
        idt[44].selector == 0x08U && idt[44].flags == 0x8EU;
    const bool cascade_enabled = (in8(0x21) & 0x04U) == 0;
    const bool irq12_enabled = (in8(0xA1) & 0x10U) == 0;
    return idt_ready && cascade_enabled && irq12_enabled;
}

void inject_mouse_packet_to_decoder(uint8_t status, uint8_t delta_x, uint8_t delta_y) {
    mouse_byte(status);
    mouse_byte(delta_x);
    mouse_byte(delta_y);
}

int32_t mouse_screen_from_logical_x(int32_t logical_x) {
    return graphics::viewport_x + static_cast<int32_t>((static_cast<uint32_t>(logical_x) * static_cast<uint32_t>(graphics::viewport_width)) / graphics::width);
}

int32_t mouse_screen_from_logical_y(int32_t logical_y) {
    return graphics::viewport_y + static_cast<int32_t>((static_cast<uint32_t>(logical_y) * static_cast<uint32_t>(graphics::viewport_height)) / graphics::height);
}

bool mouse_decoder_regression() {
    if (!mouse_online || !graphics::active) return false;

    disable_interrupts();
    mouse_packet_index = 0U;
    graphics::mouse_screen_x = mouse_screen_from_logical_x(static_cast<int32_t>(graphics::width / 2U));
    graphics::mouse_screen_y = mouse_screen_from_logical_y(static_cast<int32_t>(graphics::height / 2U));
    graphics::update_logical_mouse();
    graphics::window_x = 150;
    graphics::window_y = 88;
    graphics::mouse_buttons = 0U;
    graphics::dragging = false;
    graphics::first_mouse_packet = true;
    graphics::drag_reported = false;
    graphics::render_scene();
    graphics::present_full();

    inject_mouse_packet_to_decoder(0x08U, 1U, 1U);
    const bool packet_ok = !graphics::first_mouse_packet && mouse_packet_index == 0U;

    graphics::mouse_screen_x = mouse_screen_from_logical_x(200);
    graphics::mouse_screen_y = mouse_screen_from_logical_y(104);
    graphics::update_logical_mouse();
    inject_mouse_packet_to_decoder(0x09U, 0U, 0U);
    const int32_t press_x = graphics::mouse_x;
    const int32_t press_y = graphics::mouse_y;
    inject_mouse_packet_to_decoder(0x29U, 80U, 226U);
    const int32_t expected_x = 150 + graphics::mouse_x - press_x;
    const int32_t expected_y = 88 + graphics::mouse_y - press_y;
    inject_mouse_packet_to_decoder(0x08U, 0U, 0U);

    const bool drag_ok = graphics::drag_reported && mouse_packet_index == 0U &&
        graphics::window_x == expected_x && graphics::window_y == expected_y;
    enable_interrupts();
    return packet_ok && drag_ok;
}
