void pic_timer_only() {
    // During early boot only the PIT clock may interrupt storage and memory init.
    // Keep keyboard, cascade and all slave IRQs masked until PS/2 setup runs.
    out8(0x21, 0xFEU);
    out8(0xA1, 0xFFU);
}

void pic_enable_input_irqs() {
    // Match the established runtime mask: PIT, keyboard, cascade and PS/2 mouse.
    out8(0x21, 0xF8U);
    out8(0xA1, 0xEFU);
}
