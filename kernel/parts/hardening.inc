namespace process {
uint32_t fault_count = 0;
uint32_t last_fault_vector = 0;
uint32_t last_fault_error = 0;
uint32_t last_fault_eip = 0;
uint32_t last_fault_address = 0;
}

extern "C" uint32_t saved_kernel_esp;

const char* exception_name(uint32_t vector) {
    switch (vector) {
        case 0: return "divide error";
        case 3: return "breakpoint";
        case 4: return "overflow";
        case 5: return "bounds";
        case 6: return "invalid opcode";
        case 7: return "device unavailable";
        case 13: return "general protection";
        case 14: return "page fault";
        case 16: return "floating-point";
        case 17: return "alignment check";
        default: return "CPU exception";
    }
}

extern "C" uint32_t process_fault_dispatch(InterruptFrame* frame) {
    if (!frame || saved_kernel_esp == 0 || (frame->cs & 3U) != 3U) return 0;

    process::last_fault_vector = frame->vector;
    process::last_fault_error = frame->error;
    process::last_fault_eip = frame->eip;
    process::last_fault_address = 0;
    if (frame->vector == 14) asm volatile("mov %%cr2, %0" : "=r"(process::last_fault_address));
    ++process::fault_count;
    process::last_exit_code = 128U + frame->vector;

    serial::write("APP_FAULT_RECOVERED vector=");
    char digits[11]; uint32_t count = 0, value = frame->vector;
    do { digits[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value && count < sizeof(digits));
    while (count) serial::put(digits[--count]);
    serial::write(" error=");
    serial::write("0x");
    static constexpr char hex_table[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) serial::put(hex_table[(frame->error >> shift) & 0xF]);
    serial::write(" eip=0x");
    for (int shift = 28; shift >= 0; shift -= 4) serial::put(hex_table[(frame->eip >> shift) & 0xF]);
    if (frame->vector == 14) {
        serial::write(" cr2=0x");
        for (int shift = 28; shift >= 0; shift -= 4) serial::put(hex_table[(process::last_fault_address >> shift) & 0xF]);
    }
    serial::write("\r\n");

    console::warning("Application terminated safely after a user-mode exception.");
    console::write("  Cause         "); console::line(exception_name(frame->vector));
    console::write("  Vector        "); console::unsigned_dec(frame->vector); console::put('\n');
    console::write("  Error         "); console::hex(frame->error); console::put('\n');
    console::write("  User EIP      "); console::hex(frame->eip); console::put('\n');
    if (frame->vector == 14) {
        console::write("  Fault address "); console::hex(process::last_fault_address); console::put('\n');
    }
    console::success("KERNEL_SURVIVED_USER_FAULT");
    return 1;
}
