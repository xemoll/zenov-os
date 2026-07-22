constexpr bool ata_capacity_revalidated(uint32_t expected_sectors, uint32_t observed_sectors) {
    return observed_sectors != 0U && (expected_sectors == 0U || observed_sectors == expected_sectors);
}
