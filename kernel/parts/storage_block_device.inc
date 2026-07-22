using BlockReadSector = BlockResult (*)(uint32_t, uint8_t*);
using BlockWriteSector = BlockResult (*)(uint32_t, const uint8_t*);

struct BlockDevice {
    const char* name;
    bool present;
    bool writable;
    uint32_t sectors;
    BlockReadSector read_sector;
    BlockWriteSector write_sector;
};

constexpr bool block_device_contract_valid(const BlockDevice& device) {
    if (!device.name || !device.read_sector) return false;
    if (device.writable && !device.write_sector) return false;
    if (device.present && device.sectors == 0U) return false;
    return true;
}
