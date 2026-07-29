#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "zenov_audit_format.hpp"
#include "../security/zvrt_crypto_material.hpp"

namespace {
constexpr std::uint32_t kSectorSize = 512U, kEntryCount = 128U, kDataStart = 32U, kSlotSectors = 128U;
constexpr std::uint32_t kChunkSize = 4096U, kMaxLeaves = 16U;
#pragma pack(push, 1)
struct Superblock { char magic[8]; std::uint32_t version, total_sectors, entry_count, entry_sectors, data_start, slot_sectors, generation; char label[16]; std::uint8_t reserved[460]; };
struct Entry { std::uint8_t used, type; std::uint16_t flags; char path[48]; std::uint32_t size, checksum, reserved; };
struct Header {
    char magic[4]; std::uint16_t schema, header_size;
    std::uint32_t manifest_version, minimum_engine, record_count, payload_size;
    std::uint32_t chunk_size, flags;
    std::uint8_t payload_sha256[32], key_id[8], reserved[8];
};
struct Record {
    char path[48]; std::uint32_t file_size, chunk_size, leaf_count, flags;
    std::uint8_t merkle_root[32], file_sha256[32];
};
#pragma pack(pop)
static_assert(sizeof(Superblock) == kSectorSize && sizeof(Entry) == 64U);
static_assert(sizeof(Header) == 80U && sizeof(Record) == 128U);
using Digest = std::array<std::uint8_t, 32>;

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open image");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("invalid image size");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read image");
    return bytes;
}

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0U; i < size; ++i) { hash ^= data[i]; hash *= 16777619U; }
    return hash;
}

const Entry& find_entry(const std::vector<std::uint8_t>& disk, const char* path, std::uint32_t& index) {
    if (disk.size() < kSectorSize + kEntryCount * sizeof(Entry) || std::memcmp(disk.data(), "ZENOVFS1", 8U) != 0) {
        throw std::runtime_error("not a ZenovFS1 image");
    }
    const auto* entries = reinterpret_cast<const Entry*>(disk.data() + kSectorSize);
    for (std::uint32_t i = 0U; i < kEntryCount; ++i) {
        if (entries[i].used && entries[i].type == 1U && std::strncmp(entries[i].path, path, sizeof(entries[i].path)) == 0) {
            index = i;
            return entries[i];
        }
    }
    throw std::runtime_error(std::string("file not found: ") + path);
}

const std::uint8_t* file_bytes(const std::vector<std::uint8_t>& disk, const Entry& entry, std::uint32_t index) {
    const std::size_t offset = static_cast<std::size_t>(kDataStart + index * kSlotSectors) * kSectorSize;
    if (offset + entry.size > disk.size()) throw std::runtime_error("file slot exceeds image");
    const auto* data = disk.data() + offset;
    if (fnv1a(data, entry.size) != entry.checksum) throw std::runtime_error("ZenovFS checksum mismatch");
    return data;
}

void append_le32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

Digest leaf_digest(const std::string& path, std::uint32_t file_size, std::uint32_t index,
                   const std::uint8_t* data, std::uint32_t size) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(2U + path.size() + 12U + size);
    encoded.push_back(0U);
    encoded.push_back(static_cast<std::uint8_t>(path.size()));
    encoded.insert(encoded.end(), path.begin(), path.end());
    append_le32(encoded, file_size);
    append_le32(encoded, index);
    append_le32(encoded, size);
    encoded.insert(encoded.end(), data, data + size);
    return zenov_audit_host::sha256(encoded.data(), encoded.size());
}

Digest parent_digest(const Digest& left, const Digest& right) {
    std::array<std::uint8_t, 65> encoded{};
    encoded[0] = 1U;
    std::memcpy(encoded.data() + 1U, left.data(), left.size());
    std::memcpy(encoded.data() + 33U, right.data(), right.size());
    return zenov_audit_host::sha256(encoded.data(), encoded.size());
}

Digest merkle_root(const std::string& path, const std::uint8_t* data, std::uint32_t size, std::uint32_t leaves) {
    if (!size || !leaves || leaves > kMaxLeaves || leaves != (size + kChunkSize - 1U) / kChunkSize) {
        throw std::runtime_error("invalid Merkle leaf geometry");
    }
    std::array<Digest, kMaxLeaves> nodes{};
    for (std::uint32_t index = 0U; index < leaves; ++index) {
        const std::uint32_t offset = index * kChunkSize;
        const std::uint32_t remaining = size - offset;
        const std::uint32_t length = remaining < kChunkSize ? remaining : kChunkSize;
        nodes[index] = leaf_digest(path, size, index, data + offset, length);
    }
    std::uint32_t count = leaves;
    while (count > 1U) {
        const std::uint32_t next = (count + 1U) / 2U;
        for (std::uint32_t i = 0U; i < next; ++i) {
            const std::uint32_t left = i * 2U;
            const std::uint32_t right = left + 1U < count ? left + 1U : left;
            nodes[i] = parent_digest(nodes[left], nodes[right]);
        }
        count = next;
    }
    return nodes[0];
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: zenovfs-zvrt-verify <zenov-data.img>\n";
            return 2;
        }
        if (!zenov_audit_host::sha256_self_test()) throw std::runtime_error("SHA-256 self-test failed");
        const auto disk = read_all(argv[1]);
        std::uint32_t manifest_index = 0U;
        const Entry& manifest_entry = find_entry(disk, "/security/verified-reads.zvrt", manifest_index);
        const std::uint8_t* manifest = file_bytes(disk, manifest_entry, manifest_index);
        if (manifest_entry.size != 848U) throw std::runtime_error("unexpected ZVRT manifest size");
        const auto* header = reinterpret_cast<const Header*>(manifest);
        if (std::memcmp(header->magic, "ZVRT", 4U) != 0 || header->schema != 1U || header->header_size != sizeof(Header) ||
            header->manifest_version != 1U || header->record_count != 4U || header->payload_size != 512U ||
            header->chunk_size != kChunkSize || header->flags != 1U ||
            sizeof(Header) + header->payload_size + 256U != manifest_entry.size ||
            std::memcmp(header->key_id, kZvrtRootKeyId, sizeof(header->key_id)) != 0) {
            throw std::runtime_error("invalid ZVRT manifest header");
        }
        const auto payload_hash = zenov_audit_host::sha256(manifest + sizeof(Header), header->payload_size);
        if (std::memcmp(payload_hash.data(), header->payload_sha256, payload_hash.size()) != 0) {
            throw std::runtime_error("ZVRT payload digest mismatch");
        }
        const auto* records = reinterpret_cast<const Record*>(manifest + sizeof(Header));
        std::uint32_t total_leaves = 0U, multichunk = 0U;
        for (std::uint32_t i = 0U; i < header->record_count; ++i) {
            const Record& record = records[i];
            std::uint32_t index = 0U;
            const Entry& entry = find_entry(disk, record.path, index);
            const std::uint8_t* data = file_bytes(disk, entry, index);
            if (entry.size != record.file_size || record.chunk_size != kChunkSize ||
                record.leaf_count != (record.file_size + kChunkSize - 1U) / kChunkSize || record.flags != 3U) {
                throw std::runtime_error("ZVRT record geometry mismatch");
            }
            const auto file_hash = zenov_audit_host::sha256(data, entry.size);
            const auto root = merkle_root(record.path, data, entry.size, record.leaf_count);
            if (std::memcmp(file_hash.data(), record.file_sha256, file_hash.size()) != 0 ||
                std::memcmp(root.data(), record.merkle_root, root.size()) != 0) {
                throw std::runtime_error(std::string("ZVRT protected file mismatch: ") + record.path);
            }
            total_leaves += record.leaf_count;
            if (record.leaf_count > 1U) ++multichunk;
        }
        if (total_leaves != 5U || multichunk != 1U) throw std::runtime_error("unexpected ZVRT image shape");
        std::cout << "ZENOV_ZVRT_IMAGE_OK version=1 records=4 leaves=5 multichunk=1 key=d28215ec62269ffc\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenovfs-zvrt-verify: " << error.what() << "\n";
        return 1;
    }
}
