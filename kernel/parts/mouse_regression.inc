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

bool mouse_decoder_regression() {
    if (!mouse_online || !graphics::active) return false;

    disable_interrupts();
    mouse_packet_index = 0U;
    graphics::mouse_x = static_cast<int32_t>(graphics::width / 2U);
    graphics::mouse_y = static_cast<int32_t>(graphics::height / 2U);
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

    inject_mouse_packet_to_decoder(0x08U, 0U, 95U);
    inject_mouse_packet_to_decoder(0x08U, 0U, 95U);
    inject_mouse_packet_to_decoder(0x09U, 0U, 0U);
    inject_mouse_packet_to_decoder(0x29U, 80U, 226U);
    inject_mouse_packet_to_decoder(0x08U, 0U, 0U);

    const bool drag_ok = graphics::drag_reported && mouse_packet_index == 0U &&
        graphics::window_x == 230 && graphics::window_y == 118;
    enable_interrupts();
    return packet_ok && drag_ok;
}
