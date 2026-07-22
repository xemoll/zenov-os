constexpr uint16_t ata2_data = 0x1F0U;
constexpr uint16_t ata2_error = 0x1F1U;
constexpr uint16_t ata2_sector_count = 0x1F2U;
constexpr uint16_t ata2_lba_low = 0x1F3U;
constexpr uint16_t ata2_lba_mid = 0x1F4U;
constexpr uint16_t ata2_lba_high = 0x1F5U;
constexpr uint16_t ata2_drive = 0x1F6U;
constexpr uint16_t ata2_status = 0x1F7U;
constexpr uint16_t ata2_command = 0x1F7U;
constexpr uint16_t ata2_control = 0x3F6U;
constexpr uint8_t ata2_control_nien = 0x02U;
constexpr uint8_t ata2_control_srst = 0x04U;
constexpr uint8_t ata2_command_identify = 0xECU;
constexpr uint8_t ata2_command_read_sectors = 0x20U;
constexpr uint8_t ata2_command_write_sectors = 0x30U;
constexpr uint8_t ata2_command_flush_cache = 0xE7U;
constexpr uint32_t ata2_clock_hz = 100U;
constexpr uint32_t ata2_command_timeout_ms = 3000U;
constexpr uint32_t ata2_reset_timeout_ms = 5000U;
constexpr uint8_t ata2_max_attempts = 2U;

inline uint16_t ata2_in16(uint16_t port) {
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
inline void ata2_out16(uint16_t port, uint16_t value) { asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }

struct AtaDiagnostics {
    uint32_t commands;
    uint32_t retries;
    uint32_t resets;
    uint32_t reset_failures;
    uint32_t timeouts;
    uint32_t errors;
};

AtaDiagnostics ata_diagnostics{};
ata_policy::Result ata_last_result{ata_policy::Error::none, 0U, 0U, 0U};

ata_policy::Result ata2_result(ata_policy::Error error, uint8_t status, uint8_t error_register, uint8_t attempts) {
    return ata_policy::Result{error, status, error_register, attempts};
}
void ata2_pause() { asm volatile("pause"); }
uint32_t ata2_deadline(uint32_t timeout_ms) { return ata_policy::deadline_after(timer_ticks, timeout_ms, ata2_clock_hz); }

ata_policy::Result ata2_wait(bool require_drq, uint32_t timeout_ms, uint8_t attempts) {
    const uint32_t deadline = ata2_deadline(timeout_ms);
    for (;;) {
        const uint8_t status = in8(ata2_status);
        const uint8_t error_register = (status & ata_policy::status_error) != 0U ? in8(ata2_error) : 0U;
        const ata_policy::Error error = ata_policy::classify_device_error(status, error_register);
        if (error != ata_policy::Error::none) return ata2_result(error, status, error_register, attempts);
        if ((status & ata_policy::status_busy) == 0U && (!require_drq || (status & ata_policy::status_drq) != 0U)) {
            return ata2_result(ata_policy::Error::none, status, 0U, attempts);
        }
        if (ata_policy::deadline_reached(timer_ticks, deadline)) {
            return ata2_result((status & ata_policy::status_busy) != 0U ? ata_policy::Error::timeout_busy : ata_policy::Error::timeout_drq,
                               status, error_register, attempts);
        }
        ata2_pause();
    }
}

void ata2_record_failure(const char* operation, const ata_policy::Result& result) {
    ata_last_result = result;
    if (result.error == ata_policy::Error::timeout_busy || result.error == ata_policy::Error::timeout_drq) ++ata_diagnostics.timeouts;
    else ++ata_diagnostics.errors;
    serial::write("ATA_IO_ERROR op="); serial::write(operation); serial::write(" reason=");
    serial::line(ata_policy::error_name(result.error));
}
void ata2_record_success(const ata_policy::Result& result) { ata_last_result = result; }

ata_policy::Result ata2_soft_reset(uint8_t attempts) {
    out8(ata2_control, static_cast<uint8_t>(ata2_control_nien | ata2_control_srst));
    io_wait(); io_wait(); io_wait(); io_wait();
    out8(ata2_control, ata2_control_nien);
    io_wait(); io_wait(); io_wait(); io_wait();
    ata_policy::Result result = ata2_wait(false, ata2_reset_timeout_ms, attempts);
    if (!result.ok()) {
        ++ata_diagnostics.reset_failures;
        result.error = ata_policy::Error::reset_failed;
        ata2_record_failure("reset", result);
        return result;
    }
    ++ata_diagnostics.resets;
    serial::line("ATA_RECOVERY_RESET_OK");
    return result;
}

void ata2_select(uint32_t lba) {
    out8(ata2_drive, static_cast<uint8_t>(0xE0U | ((lba >> 24U) & 0x0FU)));
    io_wait();
}
void ata2_program_lba28(uint32_t lba) {
    out8(ata2_sector_count, 1U);
    out8(ata2_lba_low, static_cast<uint8_t>(lba));
    out8(ata2_lba_mid, static_cast<uint8_t>(lba >> 8U));
    out8(ata2_lba_high, static_cast<uint8_t>(lba >> 16U));
}
