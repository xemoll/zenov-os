constexpr uint32_t sector_size = 512U;
constexpr uint16_t ata_data = 0x1F0, ata_sector_count = 0x1F2, ata_lba_low = 0x1F3, ata_lba_mid = 0x1F4, ata_lba_high = 0x1F5;
constexpr uint16_t ata_drive = 0x1F6, ata_status = 0x1F7, ata_command = 0x1F7, ata_control = 0x3F6;
constexpr uint8_t ata_busy = 0x80, ata_drq = 0x08, ata_error_bit = 0x01;
inline uint16_t in16(uint16_t port) { uint16_t value; asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }
inline void out16(uint16_t port, uint16_t value) { asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
bool wait_not_busy() { for (uint32_t timeout = 0; timeout < 1000000; ++timeout) if ((in8(ata_status) & ata_busy) == 0) return true; return false; }
bool wait_drq() { for (uint32_t timeout = 0; timeout < 1000000; ++timeout) { const uint8_t status = in8(ata_status); if (status & ata_error_bit) return false; if ((status & ata_busy) == 0 && (status & ata_drq)) return true; } return false; }
bool ata_present = false; uint32_t ata_sector_total = 0;
void ata_select(uint32_t lba) { out8(ata_drive, static_cast<uint8_t>(0xE0U | ((lba >> 24U) & 0x0FU))); io_wait(); }
bool ata_identify() {
    out8(ata_control, 0x02); out8(ata_drive, 0xE0); io_wait(); out8(ata_sector_count, 0); out8(ata_lba_low, 0); out8(ata_lba_mid, 0); out8(ata_lba_high, 0); out8(ata_command, 0xEC);
    const uint8_t initial = in8(ata_status);
    if (initial == 0 || initial == 0xFF || !wait_not_busy() || in8(ata_lba_mid) || in8(ata_lba_high) || !wait_drq()) return false;
    uint16_t identify[256];
    for (uint32_t i = 0; i < 256; ++i) identify[i] = in16(ata_data);
    ata_sector_total = static_cast<uint32_t>(identify[60]) | (static_cast<uint32_t>(identify[61]) << 16U);
    ata_present = ata_sector_total > 0;
    return ata_present;
}
BlockResult ata_read_sector(uint32_t lba, uint8_t* output) {
    if (!output) return block_result(BlockOperation::read, BlockStatus::invalid_argument, lba, 0U, 0U, 0U);
    if (!ata_present) return block_result(BlockOperation::read, BlockStatus::no_device, lba, 0U, 0U, 0U);
    if (lba >= ata_sector_total) return block_result(BlockOperation::read, BlockStatus::out_of_range, lba, 0U, 0U, 0U);
    if (!wait_not_busy()) return block_result(BlockOperation::read, BlockStatus::timeout_busy, lba, 1U, in8(ata_status), 0U);
    ata_select(lba);
    out8(ata_sector_count, 1); out8(ata_lba_low, static_cast<uint8_t>(lba)); out8(ata_lba_mid, static_cast<uint8_t>(lba >> 8U)); out8(ata_lba_high, static_cast<uint8_t>(lba >> 16U)); out8(ata_command, 0x20);
    if (!wait_drq()) {
        const uint8_t status = in8(ata_status);
        return block_result(BlockOperation::read,
                            (status & ata_error_bit) != 0U ? BlockStatus::device_error : BlockStatus::timeout_drq,
                            lba, 1U, status, 0U);
    }
    for (uint32_t i = 0; i < 256; ++i) {
        const uint16_t value = in16(ata_data);
        output[i * 2] = static_cast<uint8_t>(value);
        output[i * 2 + 1] = static_cast<uint8_t>(value >> 8U);
    }
    return block_success(BlockOperation::read, lba, 1U);
}
BlockResult ata_write_sector(uint32_t lba, const uint8_t* input) {
    if (!input) return block_result(BlockOperation::write, BlockStatus::invalid_argument, lba, 0U, 0U, 0U);
    if (!ata_present) return block_result(BlockOperation::write, BlockStatus::no_device, lba, 0U, 0U, 0U);
    if (lba >= ata_sector_total) return block_result(BlockOperation::write, BlockStatus::out_of_range, lba, 0U, 0U, 0U);
    if (!wait_not_busy()) return block_result(BlockOperation::write, BlockStatus::timeout_busy, lba, 1U, in8(ata_status), 0U);
    ata_select(lba);
    out8(ata_sector_count, 1); out8(ata_lba_low, static_cast<uint8_t>(lba)); out8(ata_lba_mid, static_cast<uint8_t>(lba >> 8U)); out8(ata_lba_high, static_cast<uint8_t>(lba >> 16U)); out8(ata_command, 0x30);
    if (!wait_drq()) {
        const uint8_t status = in8(ata_status);
        return block_result(BlockOperation::write,
                            (status & ata_error_bit) != 0U ? BlockStatus::device_error : BlockStatus::timeout_drq,
                            lba, 1U, status, 0U);
    }
    for (uint32_t i = 0; i < 256; ++i) out16(ata_data, static_cast<uint16_t>(input[i * 2]) | static_cast<uint16_t>(input[i * 2 + 1] << 8U));
    out8(ata_command, 0xE7);
    if (!wait_not_busy()) return block_result(BlockOperation::flush, BlockStatus::timeout_busy, lba, 1U, in8(ata_status), 0U);
    const uint8_t status = in8(ata_status);
    if ((status & ata_error_bit) != 0U) return block_result(BlockOperation::flush, BlockStatus::device_error, lba, 1U, status, 0U);
    return block_success(BlockOperation::write, lba, 1U);
}
BlockDevice ata0{"ata0", false, true, 0, ata_read_sector, ata_write_sector};
