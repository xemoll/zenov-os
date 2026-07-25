#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace storage {using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;
constexpr uint32_t sector_size = 512U;
#pragma pack(push, 1)
struct ZfsSuperblock { char magic[8]; uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation; char label[16]; uint8_t reserved[460]; };
struct ZfsEntry { uint8_t used, type; uint16_t flags; char path[48]; uint32_t size, checksum, reserved; };
#pragma pack(pop)
static_assert(sizeof(ZfsSuperblock) == sector_size && sizeof(ZfsEntry) == 64U);
constexpr uint32_t zfs_max_entries = 128U, transaction_none = 0xFFFFFFFFU;
constexpr uint8_t zfs_type_file = 1U, zfs_type_directory = 2U, zfs_type_transaction = 3U;
constexpr uint16_t zfs_flag_committed = 0x0001U;
#include "../kernel/parts/storage_format.inc"
}

namespace {
using storage::ZfsEntry;
using storage::ZfsSuperblock;
constexpr std::uint32_t kDeviceSectors = 32768U;

void set_text(char* output, std::size_t capacity, const char* text) {
    const std::size_t length = std::strlen(text);
    if (length >= capacity) throw std::runtime_error("test fixture text too long");
    std::memset(output, 0, capacity);
    std::memcpy(output, text, length);
}

ZfsSuperblock valid_superblock() {
    ZfsSuperblock super{};
    std::memcpy(super.magic, "ZENOVFS1", 8U);
    super.version = 1U;
    super.total_sectors = kDeviceSectors;
    super.entry_count = storage::zfs_max_entries;
    super.entry_sectors = 16U;
    super.data_start = 32U;
    super.slot_sectors = 128U;
    super.generation = 1U;
    set_text(super.label, sizeof(super.label), "ZENOVDATA");
    return super;
}

std::array<ZfsEntry, storage::zfs_max_entries> valid_entries() {
    std::array<ZfsEntry, storage::zfs_max_entries> entries{};
    entries[0].used = 1U; entries[0].type = storage::zfs_type_directory; set_text(entries[0].path, sizeof(entries[0].path), "/apps");
    entries[1].used = 1U; entries[1].type = storage::zfs_type_file; entries[1].size = 128U; entries[1].checksum = 0x12345678U; set_text(entries[1].path, sizeof(entries[1].path), "/apps/tool.elf");
    return entries;
}

void require_super_rejected(ZfsSuperblock super, const char* expected) {
    const char* reason = nullptr;
    if (storage::zfs_format_superblock_valid(super, kDeviceSectors, reason) || !reason || std::strcmp(reason, expected) != 0) throw std::runtime_error(std::string("superblock case did not reject as ") + expected);
}

void require_entries_rejected(const ZfsSuperblock& super, std::array<ZfsEntry, storage::zfs_max_entries> entries, const char* expected) {
    const char* reason = nullptr;
    if (storage::zfs_format_entries_valid(super, entries.data(), reason) || !reason || std::strcmp(reason, expected) != 0) throw std::runtime_error(std::string("entry case did not reject as ") + expected);
}
}

int main() {
    try {
        ZfsSuperblock super = valid_superblock();
        auto entries = valid_entries();
        const char* reason = nullptr;
        if (!storage::zfs_format_superblock_valid(super, kDeviceSectors, reason) || !storage::zfs_format_entries_valid(super, entries.data(), reason)) throw std::runtime_error("valid baseline rejected");

        { auto value = super; value.entry_sectors = 17U; require_super_rejected(value, "entry-sectors"); }
        { auto value = super; value.data_start = 16U; require_super_rejected(value, "metadata-layout"); }
        { auto value = super; value.slot_sectors = 0U; require_super_rejected(value, "slot-size"); }
        { auto value = super; value.slot_sectors = 129U; require_super_rejected(value, "slot-size"); }
        { auto value = super; value.total_sectors = kDeviceSectors + 1U; require_super_rejected(value, "device-size"); }
        { auto value = super; value.data_start = 32700U; require_super_rejected(value, "data-layout"); }
        { auto value = super; value.generation = 0U; require_super_rejected(value, "generation"); }
        { auto value = super; value.reserved[0] = 1U; require_super_rejected(value, "superblock-reserved"); }
        { auto value = super; std::memset(value.label, 'A', sizeof(value.label)); require_super_rejected(value, "label"); }

        { auto value = entries; value[1].used = 2U; require_entries_rejected(super, value, "entry-used"); }
        { auto value = entries; std::memset(value[1].path, 'A', sizeof(value[1].path)); value[1].path[0] = '/'; require_entries_rejected(super, value, "entry-path"); }
        { auto value = entries; set_text(value[1].path, sizeof(value[1].path), "/apps/bad\nname"); require_entries_rejected(super, value, "entry-path"); }
        { auto value = entries; set_text(value[1].path, sizeof(value[1].path), "/apps/../tool"); require_entries_rejected(super, value, "entry-path"); }
        { auto value = entries; set_text(value[1].path, sizeof(value[1].path), "/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a/a"); require_entries_rejected(super, value, "entry-path"); }
        { auto value = entries; value[1].size = 65537U; require_entries_rejected(super, value, "file-size"); }
        { auto value = entries; value[0].size = 1U; require_entries_rejected(super, value, "directory-metadata"); }
        { auto value = entries; value[2] = value[1]; require_entries_rejected(super, value, "duplicate-path"); }
        { auto value = entries; set_text(value[1].path, sizeof(value[1].path), "/missing/tool"); require_entries_rejected(super, value, "missing-parent"); }
        { auto value = entries; value[2] = value[1]; value[2].flags = storage::zfs_flag_committed; value[2].reserved = 0U; require_entries_rejected(super, value, "transition-old-mismatch"); }
        { auto value = entries; value[2] = value[1]; value[2].type = storage::zfs_type_transaction; value[2].reserved = 3U; require_entries_rejected(super, value, "transaction-old-missing"); }

        {
            auto value = entries;
            value[2] = value[1]; value[2].flags = storage::zfs_flag_committed; value[2].reserved = 1U;
            if (!storage::zfs_format_entries_valid(super, value.data(), reason)) throw std::runtime_error("valid committed recovery pair rejected");
        }
        {
            auto value = entries;
            value[2] = value[1]; value[2].type = storage::zfs_type_transaction; value[2].reserved = 1U;
            if (!storage::zfs_format_entries_valid(super, value.data(), reason)) throw std::runtime_error("valid transaction recovery pair rejected");
        }
        {
            auto value = entries;
            value[2] = value[1]; value[2].flags = storage::zfs_flag_committed; value[2].reserved = 1U;
            std::memset(&value[1], 0, sizeof(value[1]));
            if (!storage::zfs_format_entries_valid(super, value.data(), reason)) throw std::runtime_error("valid post-delete committed state rejected");
        }

        std::cout << "ZENOVFS_MOUNT_VALIDATION_TEST_OK cases=24 shared-kernel-validator=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-mount-validation-test: " << error.what() << '\n';        return 1;
    }
}
