namespace native_package {

constexpr uint32_t header_size = 128U;
constexpr uint32_t maximum_manifest_size = 8192U;
constexpr uint32_t maximum_dependencies = 4U;
constexpr uint32_t maximum_conflicts = 4U;
constexpr uint8_t payload_zex1 = 1U;
constexpr uint8_t payload_elf32 = 2U;

struct Manifest {
    char name[32];
    char version[16];
    char entrypoint[48];
    uint8_t payload_type;
    uint8_t dependency_count;
    uint8_t conflict_count;
    uint8_t reserved;
    char dependencies[maximum_dependencies][48];
    char conflicts[maximum_conflicts][32];
};

struct VerifiedPackage {
    Manifest manifest;
    const uint8_t* payload;
    uint32_t payload_size;
    uint8_t package_digest[32];
};

enum class Error : uint32_t {
    none,
    io,
    too_large,
    truncated,
    magic,
    version,
    flags,
    length,
    header_digest,
    manifest_digest,
    payload_digest,
    manifest_syntax,
    manifest_order,
    manifest_missing,
    manifest_value,
    unsupported,
    path,
    payload_format
};

const char* error_text(Error error) {
    switch (error) {
        case Error::none: return "none";
        case Error::io: return "I/O failure";
        case Error::too_large: return "package exceeds ZenovFS1 capacity";
        case Error::truncated: return "package header is truncated";
        case Error::magic: return "invalid ZENPKG1 magic";
        case Error::version: return "unsupported package version";
        case Error::flags: return "unsupported package flags";
        case Error::length: return "package length does not match header";
        case Error::header_digest: return "package header SHA-256 mismatch";
        case Error::manifest_digest: return "manifest SHA-256 mismatch";
        case Error::payload_digest: return "payload SHA-256 mismatch";
        case Error::manifest_syntax: return "invalid manifest syntax";
        case Error::manifest_order: return "manifest is not canonical";
        case Error::manifest_missing: return "manifest is missing required fields";
        case Error::manifest_value: return "manifest contains an invalid value";
        case Error::unsupported: return "package requirements are unsupported";
        case Error::path: return "unsafe or non-versioned package entrypoint";
        case Error::payload_format: return "payload format does not match manifest";
    }
    return "unknown package error";
}

uint32_t read_u32_le(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8U) |
           (static_cast<uint32_t>(input[2]) << 16U) |
           (static_cast<uint32_t>(input[3]) << 24U);
}

unsigned long long read_u64_le(const uint8_t* input) {
    unsigned long long value = 0;
    for (uint32_t i = 0; i < 8U; ++i) value |= static_cast<unsigned long long>(input[i]) << (i * 8U);
    return value;
}

bool bytes_equal(const uint8_t* left, const uint8_t* right, uint32_t size) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < size; ++i) difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    return difference == 0;
}

bool slice_equal(const char* data, uint32_t size, const char* literal) {
    const uint32_t literal_size = static_cast<uint32_t>(string_length(literal));
    if (size != literal_size) return false;
    for (uint32_t i = 0; i < size; ++i) if (data[i] != literal[i]) return false;
    return true;
}

int slice_compare(const char* left, uint32_t left_size, const char* right, uint32_t right_size) {
    const uint32_t common = left_size < right_size ? left_size : right_size;
    for (uint32_t i = 0; i < common; ++i) {
        const uint8_t a = static_cast<uint8_t>(left[i]);
        const uint8_t b = static_cast<uint8_t>(right[i]);
        if (a != b) return static_cast<int>(a) - static_cast<int>(b);
    }
    if (left_size == right_size) return 0;
    return left_size < right_size ? -1 : 1;
}

bool copy_slice(char* output, uint32_t capacity, const char* data, uint32_t size) {
    if (!capacity || size + 1U > capacity) return false;
    for (uint32_t i = 0; i < size; ++i) output[i] = data[i];
    output[size] = 0;
    return true;
}

bool safe_lower_identifier(const char* data, uint32_t size, uint32_t maximum) {
    if (!size || size > maximum) return false;
    if (!((data[0] >= 'a' && data[0] <= 'z') || (data[0] >= '0' && data[0] <= '9'))) return false;
    if (!((data[size - 1U] >= 'a' && data[size - 1U] <= 'z') || (data[size - 1U] >= '0' && data[size - 1U] <= '9'))) return false;
    char previous = 0;
    for (uint32_t i = 0; i < size; ++i) {
        const char value = data[i];
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_')) return false;
        if (value == '.' && previous == '.') return false;
        previous = value;
    }
    return true;
}

bool safe_version(const char* data, uint32_t size) {
    if (!size || size > 15U) return false;
    for (uint32_t i = 0; i < size; ++i) {
        const char value = data[i];
        if (!((value >= '0' && value <= '9') || value == '.' || value == '-' || value == '+' || value == '_')) return false;
    }
    return true;
}

bool printable_value(const char* data, uint32_t size, uint32_t maximum) {
    if (!size || size > maximum) return false;
    for (uint32_t i = 0; i < size; ++i) {
        const uint8_t value = static_cast<uint8_t>(data[i]);
        if (value < 0x20U || value == 0x7FU) return false;
    }
    return true;
}

bool parse_version_component(const char*& cursor, uint32_t& value) {
    if (*cursor < '0' || *cursor > '9') return false;
    value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
        if (value > (0xFFFFFFFFU - digit) / 10U) return false;
        value = value * 10U + digit;
        ++cursor;
    }
    return true;
}

bool version_supported(const char* value) {
    uint32_t parts[3]{};
    const char* cursor = value;
    for (uint32_t i = 0; i < 3U; ++i) {
        if (!parse_version_component(cursor, parts[i])) return false;
        if (i < 2U) { if (*cursor != '.') return false; ++cursor; }
    }
    if (*cursor && *cursor != '-' && *cursor != '+') return false;
    const uint32_t current[3] = {0U, 1U, 1U};
    for (uint32_t i = 0; i < 3U; ++i) {
        if (parts[i] < current[i]) return true;
        if (parts[i] > current[i]) return false;
    }
    return true;
}

bool append_text(char* output, uint32_t capacity, const char* text) {
    uint32_t length = static_cast<uint32_t>(string_length(output));
    uint32_t index = 0;
    while (text[index]) {
        if (length + 1U >= capacity) return false;
        output[length++] = text[index++];
    }
    output[length] = 0;
    return true;
}

bool expected_entrypoint(const Manifest& manifest, char output[48]) {
    output[0] = 0;
    if (!append_text(output, 48U, "/data/apps/pkg-")) return false;
    if (!append_text(output, 48U, manifest.name)) return false;
    if (!append_text(output, 48U, "-")) return false;
    if (!append_text(output, 48U, manifest.version)) return false;
    return append_text(output, 48U, manifest.payload_type == payload_zex1 ? ".zex" : ".elf");
}

int key_rank(const char* key, uint32_t size) {
    if (slice_equal(key, size, "format")) return 0;
    if (slice_equal(key, size, "name")) return 1;
    if (slice_equal(key, size, "version")) return 2;
    if (slice_equal(key, size, "architecture")) return 3;
    if (slice_equal(key, size, "target")) return 4;
    if (slice_equal(key, size, "kind")) return 5;
    if (slice_equal(key, size, "entrypoint")) return 6;
    if (slice_equal(key, size, "payload_type")) return 7;
    if (slice_equal(key, size, "runtime")) return 8;
    if (slice_equal(key, size, "min_os")) return 9;
    if (slice_equal(key, size, "license")) return 10;
    if (slice_equal(key, size, "source")) return 11;
    if (slice_equal(key, size, "asset_policy")) return 12;
    if (slice_equal(key, size, "capability")) return 13;
    if (slice_equal(key, size, "dependency")) return 14;
    if (slice_equal(key, size, "conflict")) return 15;
    return -1;
}

bool copy_requirement(char* output, uint32_t capacity, const char* value, uint32_t size) {
    if (!size || size + 1U > capacity) return false;
    for (uint32_t i = 0; i < size; ++i) {
        const char ch = value[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_' || ch == '@')) return false;
    }
    return copy_slice(output, capacity, value, size);
}

Error parse_manifest(const uint8_t* bytes, uint32_t size, Manifest& manifest) {
    memset(&manifest, 0, sizeof(manifest));
    if (!bytes || !size || size > maximum_manifest_size || bytes[size - 1U] != '\n') return Error::manifest_syntax;

    uint32_t position = 0;
    uint32_t required_mask = 0;
    int last_rank = -1;
    char last_repeated[48]{};
    uint32_t last_repeated_size = 0;
    int last_repeated_rank = -1;
    uint32_t capability_mask = 0;

    while (position < size) {
        const uint32_t line_start = position;
        while (position < size && bytes[position] != '\n') {
            if (bytes[position] == '\r' || bytes[position] == 0) return Error::manifest_syntax;
            ++position;
        }
        if (position >= size || position == line_start) return Error::manifest_syntax;
        const uint32_t line_end = position++;
        uint32_t equal = line_start;
        while (equal < line_end && bytes[equal] != '=') ++equal;
        if (equal == line_start || equal >= line_end || equal + 1U >= line_end) return Error::manifest_syntax;
        const char* key = reinterpret_cast<const char*>(bytes + line_start);
        const uint32_t key_size = equal - line_start;
        const char* value = reinterpret_cast<const char*>(bytes + equal + 1U);
        const uint32_t value_size = line_end - equal - 1U;
        const int rank = key_rank(key, key_size);
        if (rank < 0) return Error::manifest_syntax;
        if (rank < last_rank) return Error::manifest_order;
        if (rank <= 12) {
            const uint32_t bit = 1U << static_cast<uint32_t>(rank);
            if (required_mask & bit) return Error::manifest_order;
            required_mask |= bit;
        } else {
            if (rank == last_repeated_rank && slice_compare(last_repeated, last_repeated_size, value, value_size) >= 0) return Error::manifest_order;
            if (!copy_slice(last_repeated, sizeof(last_repeated), value, value_size)) return Error::manifest_value;
            last_repeated_size = value_size;
            last_repeated_rank = rank;
        }
        last_rank = rank;

        if (rank == 0 && !slice_equal(value, value_size, "zenpkg-manifest-1")) return Error::manifest_value;
        if (rank == 1 && (!safe_lower_identifier(value, value_size, 31U) || !copy_slice(manifest.name, sizeof(manifest.name), value, value_size))) return Error::manifest_value;
        if (rank == 2 && (!safe_version(value, value_size) || !copy_slice(manifest.version, sizeof(manifest.version), value, value_size))) return Error::manifest_value;
        if (rank == 3 && !slice_equal(value, value_size, "i686")) return Error::unsupported;
        if (rank == 4 && !slice_equal(value, value_size, "i686-zenov-none")) return Error::unsupported;
        if (rank == 5 && !slice_equal(value, value_size, "application") && !slice_equal(value, value_size, "native")) return Error::unsupported;
        if (rank == 6 && (!printable_value(value, value_size, 47U) || !copy_slice(manifest.entrypoint, sizeof(manifest.entrypoint), value, value_size))) return Error::path;
        if (rank == 7) {
            if (slice_equal(value, value_size, "zex1")) manifest.payload_type = payload_zex1;
            else if (slice_equal(value, value_size, "elf32")) manifest.payload_type = payload_elf32;
            else return Error::unsupported;
        }
        if (rank == 8 && !slice_equal(value, value_size, "native")) return Error::unsupported;
        if (rank == 9) {
            char minimum[16]{};
            if (!safe_version(value, value_size) || !copy_slice(minimum, sizeof(minimum), value, value_size) || !version_supported(minimum)) return Error::unsupported;
        }
        if (rank == 10 && !printable_value(value, value_size, 96U)) return Error::manifest_value;
        if (rank == 11 && !printable_value(value, value_size, 256U)) return Error::manifest_value;
        if (rank == 12 && !slice_equal(value, value_size, "redistributable")) return Error::unsupported;
        if (rank == 13) {
            if (slice_equal(value, value_size, "abi.zex1.v1")) capability_mask |= 1U;
            else if (slice_equal(value, value_size, "abi.elf32.i386.static")) capability_mask |= 2U;
            else if (slice_equal(value, value_size, "kernel.ring3")) capability_mask |= 4U;
            else if (slice_equal(value, value_size, "storage.zenovfs1")) capability_mask |= 8U;
            else return Error::unsupported;
        }
        if (rank == 14) {
            if (manifest.dependency_count >= maximum_dependencies || !copy_requirement(manifest.dependencies[manifest.dependency_count], 48U, value, value_size)) return Error::manifest_value;
            ++manifest.dependency_count;
        }
        if (rank == 15) {
            if (manifest.conflict_count >= maximum_conflicts || !copy_requirement(manifest.conflicts[manifest.conflict_count], 32U, value, value_size)) return Error::manifest_value;
            ++manifest.conflict_count;
        }
    }

    if (required_mask != ((1U << 13U) - 1U)) return Error::manifest_missing;
    const uint32_t required_capabilities = (manifest.payload_type == payload_zex1 ? 1U : 2U) | 4U | 8U;
    if ((capability_mask & required_capabilities) != required_capabilities) return Error::unsupported;
    char expected[48]{};
    if (!expected_entrypoint(manifest, expected) || !string_equal(expected, manifest.entrypoint)) return Error::path;
    return Error::none;
}

bool payload_shape_valid(const Manifest& manifest, const uint8_t* payload, uint32_t size) {
    if (!payload || size < 4U) return false;
    if (manifest.payload_type == payload_zex1) return payload[0] == 'Z' && payload[1] == 'E' && payload[2] == 'X' && payload[3] == '1';
    return payload[0] == 0x7FU && payload[1] == 'E' && payload[2] == 'L' && payload[3] == 'F';
}

Error verify_bytes(const uint8_t* bytes, uint32_t size, VerifiedPackage& package) {
    memset(&package, 0, sizeof(package));
    if (!bytes || size < header_size) return Error::truncated;
    const uint8_t expected_magic[8] = {'Z','E','N','P','K','G','1',0};
    if (!bytes_equal(bytes, expected_magic, sizeof(expected_magic))) return Error::magic;
    if (read_u32_le(bytes + 8U) != 1U) return Error::version;
    if (read_u32_le(bytes + 12U) != 0U) return Error::flags;
    const unsigned long long manifest_size64 = read_u64_le(bytes + 16U);
    const unsigned long long payload_size64 = read_u64_le(bytes + 24U);
    if (manifest_size64 > maximum_manifest_size || payload_size64 > 0xFFFFFFFFULL) return Error::too_large;
    const unsigned long long total = static_cast<unsigned long long>(header_size) + manifest_size64 + payload_size64;
    if (total != size) return Error::length;
    const uint32_t manifest_size = static_cast<uint32_t>(manifest_size64);
    const uint32_t payload_size = static_cast<uint32_t>(payload_size64);

    uint8_t digest[32]{};
    crypto::sha256(bytes, 96U, digest);
    if (!crypto::digest_equal(digest, bytes + 96U)) return Error::header_digest;
    crypto::sha256(bytes + header_size, manifest_size, digest);
    if (!crypto::digest_equal(digest, bytes + 32U)) return Error::manifest_digest;
    const uint8_t* payload = bytes + header_size + manifest_size;
    crypto::sha256(payload, payload_size, digest);
    if (!crypto::digest_equal(digest, bytes + 64U)) return Error::payload_digest;

    const Error manifest_error = parse_manifest(bytes + header_size, manifest_size, package.manifest);
    if (manifest_error != Error::none) return manifest_error;
    if (!payload_shape_valid(package.manifest, payload, payload_size)) return Error::payload_format;
    package.payload = payload;
    package.payload_size = payload_size;
    crypto::sha256(bytes, size, package.package_digest);
    return Error::none;
}

Error verify_file(const char* path, uint8_t* buffer, uint32_t capacity, VerifiedPackage& package, uint32_t& package_size) {
    package_size = 0;
    if (!path || !buffer || capacity < header_size) return Error::io;
    storage::FileInfo info{};
    if (!storage::query(path, info) || info.type != storage::zfs_type_file) return Error::io;
    if (info.size > capacity) return Error::too_large;
    if (!storage::read_file(path, buffer, capacity, package_size) || package_size != info.size) return Error::io;
    return verify_bytes(buffer, package_size, package);
}

} // namespace native_package
