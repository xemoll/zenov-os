bool ps2_inject_aux_byte(uint8_t value) {
    if (!ps2_command(0xD3) || !ps2_data(value)) return false;
    return true;
}

bool ps2_wait_packet_index(uint32_t expected) {
    for (uint32_t timeout = 0; timeout < 1000U; ++timeout) {
        if (mouse_packet_index == expected) return true;
        asm volatile("sti; hlt");
    }
    return false;
}

bool ps2_inject_packet(uint8_t status, uint8_t delta_x, uint8_t delta_y) {
    if (!ps2_inject_aux_byte(status) || !ps2_wait_packet_index(1U)) return false;
    if (!ps2_inject_aux_byte(delta_x) || !ps2_wait_packet_index(2U)) return false;
    if (!ps2_inject_aux_byte(delta_y) || !ps2_wait_packet_index(0U)) return false;
    return true;
}

bool ps2_mouse_irq_self_test() {
    if (!mouse_online || !graphics::active) return false;
    graphics::first_mouse_packet = true;
    graphics::drag_reported = false;

    if (!ps2_inject_packet(0x08U, 1U, 1U) || graphics::first_mouse_packet) return false;
    serial::line("PS2_MOUSE_IRQ_OK");

    if (!ps2_inject_packet(0x08U, 0U, 95U)) return false;
    if (!ps2_inject_packet(0x08U, 0U, 95U)) return false;
    if (!ps2_inject_packet(0x09U, 0U, 0U)) return false;
    if (!ps2_inject_packet(0x29U, 80U, 226U)) return false;
    if (!ps2_inject_packet(0x08U, 0U, 0U)) return false;
    if (!graphics::drag_reported) return false;

    serial::line("PS2_MOUSE_DRAG_IRQ_OK");
    return true;
}
