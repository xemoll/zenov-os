namespace ata_policy {

constexpr uint8_t status_busy = 0x80U;
constexpr uint8_t status_device_ready = 0x40U;
constexpr uint8_t status_device_fault = 0x20U;
constexpr uint8_t status_drq = 0x08U;
constexpr uint8_t status_error = 0x01U;
constexpr uint8_t error_interface_crc = 0x80U;
constexpr uint8_t error_uncorrectable = 0x40U;
constexpr uint8_t error_id_not_found = 0x10U;
constexpr uint8_t error_command_aborted = 0x04U;

enum class Error : uint8_t {
    none,
    no_device,
    invalid_argument,
    out_of_range,
    timeout_busy,
    timeout_drq,
    device_fault,
    command_aborted,
    id_not_found,
    uncorrectable,
    interface_crc,
    device_error,
    reset_failed
};

struct Result {
    Error error;
    uint8_t status;
    uint8_t error_register;
    uint8_t attempts;
    constexpr bool ok() const { return error == Error::none; }
};

constexpr uint32_t ticks_from_ms(uint32_t milliseconds, uint32_t frequency_hz) {
    if (milliseconds == 0U || frequency_hz == 0U) return 0U;
    const uint64_t scaled = static_cast<uint64_t>(milliseconds) * frequency_hz + 999U;
    const uint64_t ticks = scaled / 1000U;
    return ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(ticks);
}
constexpr uint32_t deadline_after(uint32_t now, uint32_t milliseconds, uint32_t frequency_hz) {
    return now + ticks_from_ms(milliseconds, frequency_hz);
}
constexpr bool deadline_reached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

constexpr Error classify_device_error(uint8_t status, uint8_t error_register) {
    if (status == 0U || status == 0xFFU) return Error::no_device;
    if ((status & status_device_fault) != 0U) return Error::device_fault;
    if ((status & status_error) == 0U) return Error::none;
    if ((error_register & error_interface_crc) != 0U) return Error::interface_crc;
    if ((error_register & error_uncorrectable) != 0U) return Error::uncorrectable;
    if ((error_register & error_id_not_found) != 0U) return Error::id_not_found;
    if ((error_register & error_command_aborted) != 0U) return Error::command_aborted;
    return Error::device_error;
}

constexpr bool retryable(Error error) {
    return error == Error::timeout_busy || error == Error::timeout_drq ||
           error == Error::device_fault || error == Error::command_aborted ||
           error == Error::interface_crc;
}

constexpr const char* error_name(Error error) {
    switch (error) {
        case Error::none: return "none";
        case Error::no_device: return "no-device";
        case Error::invalid_argument: return "invalid-argument";
        case Error::out_of_range: return "out-of-range";
        case Error::timeout_busy: return "timeout-busy";
        case Error::timeout_drq: return "timeout-drq";
        case Error::device_fault: return "device-fault";
        case Error::command_aborted: return "command-aborted";
        case Error::id_not_found: return "id-not-found";
        case Error::uncorrectable: return "uncorrectable";
        case Error::interface_crc: return "interface-crc";
        case Error::device_error: return "device-error";
        case Error::reset_failed: return "reset-failed";
    }
    return "unknown";
}

} // namespace ata_policy
