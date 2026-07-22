constexpr uint32_t sector_size = 512U;

bool ata_present = false;
uint32_t ata_sector_total = 0U;
BlockDevice ata0{"ata0", false, true, 0U, nullptr, nullptr};
