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

struct BlockIoState {
    uint32_t requests;
    uint32_t completed;
    uint32_t failed;
    BlockResult last_result;
    BlockResult last_failure;
};

constexpr BlockIoState block_io_state_initial() {
    return BlockIoState{0U, 0U, 0U,
                        block_success(BlockOperation::none, 0U, 0U),
                        block_success(BlockOperation::none, 0U, 0U)};
}

constexpr bool block_device_contract_valid(const BlockDevice& device) {
    if (!device.name || !device.read_sector) return false;
    if (device.writable && !device.write_sector) return false;
    if (device.present && device.sectors == 0U) return false;
    return true;
}

inline BlockResult block_io_complete(BlockIoState& state, const BlockResult& result) {
    ++state.requests;
    state.last_result = result;
    if (result.ok()) {
        ++state.completed;
    } else {
        ++state.failed;
        state.last_failure = result;
    }
    return result;
}

inline BlockResult block_device_read(BlockDevice& device, BlockIoState& state,
                                     uint32_t lba, uint8_t* output) {
    if (!device.present) {
        return block_io_complete(state, block_result(BlockOperation::read, BlockStatus::no_device,
                                                     lba, 0U, 0U, 0U));
    }
    if (!output) {
        return block_io_complete(state, block_result(BlockOperation::read, BlockStatus::invalid_argument,
                                                     lba, 0U, 0U, 0U));
    }
    if (!device.read_sector) {
        return block_io_complete(state, block_result(BlockOperation::read, BlockStatus::no_device,
                                                     lba, 0U, 0U, 0U));
    }
    if (lba >= device.sectors) {
        return block_io_complete(state, block_result(BlockOperation::read, BlockStatus::out_of_range,
                                                     lba, 0U, 0U, 0U));
    }
    return block_io_complete(state, device.read_sector(lba, output));
}

inline BlockResult block_device_write(BlockDevice& device, BlockIoState& state,
                                      uint32_t lba, const uint8_t* input) {
    if (!device.present) {
        return block_io_complete(state, block_result(BlockOperation::write, BlockStatus::no_device,
                                                     lba, 0U, 0U, 0U));
    }
    if (!input) {
        return block_io_complete(state, block_result(BlockOperation::write, BlockStatus::invalid_argument,
                                                     lba, 0U, 0U, 0U));
    }
    if (!device.writable) {
        return block_io_complete(state, block_result(BlockOperation::write, BlockStatus::read_only,
                                                     lba, 0U, 0U, 0U));
    }
    if (!device.write_sector) {
        return block_io_complete(state, block_result(BlockOperation::write, BlockStatus::no_device,
                                                     lba, 0U, 0U, 0U));
    }
    if (lba >= device.sectors) {
        return block_io_complete(state, block_result(BlockOperation::write, BlockStatus::out_of_range,
                                                     lba, 0U, 0U, 0U));
    }
    return block_io_complete(state, device.write_sector(lba, input));
}

constexpr bool block_io_counters_valid(const BlockIoState& state) {
    return state.requests == state.completed + state.failed;
}
