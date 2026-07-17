constexpr const char* state_path = "/var/lib/zenpkg/state.v1";
constexpr uint32_t maximum_packages = 8U;

struct [[gnu::packed]] PackageReference {
    uint8_t valid;
    uint8_t payload_type;
    uint8_t dependency_count;
    uint8_t conflict_count;
    uint32_t package_size;
    uint32_t payload_size;
    char version[16];
    char entrypoint[48];
    uint8_t package_digest[32];
    uint8_t payload_digest[32];
    char dependencies[native_package::maximum_dependencies][48];
    char conflicts[native_package::maximum_conflicts][32];
};

struct [[gnu::packed]] PackageRecord {
    uint8_t used;
    uint8_t reserved[3];
    char name[32];
    PackageReference active;
    PackageReference previous;
};

struct [[gnu::packed]] Database {
    char magic[8];
    uint32_t version;
    uint32_t generation;
    uint32_t count;
    uint32_t checksum;
    PackageRecord records[maximum_packages];
};

static_assert(sizeof(PackageReference) == 460U);
static_assert(sizeof(PackageRecord) == 956U);
static_assert(sizeof(Database) == 7672U);

Database database{};
uint8_t* package_buffer = nullptr;
uint8_t* verification_buffer = nullptr;
bool initialized = false;

uint32_t database_checksum(const Database& input) {
    Database copy = input;
    copy.checksum = 0;
    return storage::fnv1a(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

uint32_t record_count(const Database& input) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) if (input.records[i].used) ++count;
    return count;
}

bool requirement_text_valid(const char* text, uint32_t capacity, bool needs_version) {
    uint32_t size = 0, separators = 0;
    while (size < capacity && text[size]) { if (text[size] == '@') ++separators; ++size; }
    if (!size || size >= capacity || (needs_version && separators != 1U) || (!needs_version && separators != 0U)) return false;
    for (uint32_t i = 0; i < size; ++i) {
        const char value = text[i];
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_' || value == '@')) return false;
    }
    for (uint32_t i = size + 1U; i < capacity; ++i) if (text[i] != 0) return false;
    return true;
}

bool reference_valid(const PackageReference& reference) {
    if (!reference.valid) {
        const uint8_t zero[sizeof(PackageReference)]{};
        return memcmp(&reference, zero, sizeof(reference)) == 0;
    }
    if (reference.payload_type != native_package::payload_zex1 && reference.payload_type != native_package::payload_elf32) return false;
    if (!reference.package_size || !reference.payload_size || reference.package_size > native_package::maximum_package_size ||
        reference.payload_size > storage::file_capacity() || !reference.version[0] || !reference.entrypoint[0]) return false;
    if (reference.dependency_count > native_package::maximum_dependencies || reference.conflict_count > native_package::maximum_conflicts) return false;
    if (!string_starts_with(reference.entrypoint, "/data/apps/pkg-")) return false;
    for (uint32_t i = 0; i < native_package::maximum_dependencies; ++i) {
        if (i < reference.dependency_count) { if (!requirement_text_valid(reference.dependencies[i], 48U, true)) return false; }
        else if (reference.dependencies[i][0]) return false;
    }
    for (uint32_t i = 0; i < native_package::maximum_conflicts; ++i) {
        if (i < reference.conflict_count) { if (!requirement_text_valid(reference.conflicts[i], 32U, false)) return false; }
        else if (reference.conflicts[i][0]) return false;
    }
    return true;
}

bool reference_catalog_authorized(const char* name, const PackageReference& reference) {
    if (!reference.valid) return true;
    const package_catalog::Record* record = package_catalog::find_active(name, reference.version);
    return record && package_catalog::record_matches(*record, reference.entrypoint, reference.payload_type,
        reference.package_size, reference.payload_size, reference.package_digest, reference.payload_digest);
}

bool database_valid(const Database& input, bool require_catalog) {
    const char expected[8] = {'Z','P','K','D','B','1',0,0};
    if (!storage::bytes_equal(input.magic, expected, sizeof(expected)) || input.version != 1U ||
        input.count != record_count(input) || input.checksum != database_checksum(input)) return false;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = input.records[i];
        if (!record.used) {
            const PackageRecord zero{};
            if (memcmp(&record, &zero, sizeof(record)) != 0) return false;
            continue;
        }
        if (!record.name[0] || !reference_valid(record.active) || !record.active.valid || !reference_valid(record.previous)) return false;
        if (require_catalog && (!reference_catalog_authorized(record.name, record.active) || !reference_catalog_authorized(record.name, record.previous))) return false;
        for (uint32_t other = i + 1U; other < maximum_packages; ++other) {
            if (input.records[other].used && string_equal(record.name, input.records[other].name)) return false;
        }
    }
    return true;
}

void initialize_empty_database(Database& output) {
    memset(&output, 0, sizeof(output));
    const char magic[8] = {'Z','P','K','D','B','1',0,0};
    memcpy(output.magic, magic, sizeof(magic));
    output.version = 1U;
    output.checksum = database_checksum(output);
}

bool ensure_directory(const char* path) {
    storage::FileInfo info{};
    if (storage::query(path, info)) return info.type == storage::zfs_type_directory;
    return storage::make_directory(path);
}

bool load_database() {
    uint32_t size = 0;
    Database loaded{};
    if (!storage::read_file(state_path, reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded), size) || size != sizeof(loaded)) return false;
    if (!database_valid(loaded, true)) return false;
    database = loaded;
    return true;
}

bool persist_database(Database& next) {
    next.count = record_count(next);
    next.generation = database.generation + 1U;
    next.checksum = database_checksum(next);
    if (!database_valid(next, true)) return false;
    if (!storage::write_file(state_path, reinterpret_cast<const uint8_t*>(&next), sizeof(next), false)) return false;
    if (!storage::sync_metadata()) {
        Database recovered{};
        uint32_t recovered_size = 0;
        if (storage::read_file(state_path, reinterpret_cast<uint8_t*>(&recovered), sizeof(recovered), recovered_size) &&
            recovered_size == sizeof(recovered) && database_valid(recovered, true)) {
            database = recovered;
            return recovered.generation == next.generation && recovered.checksum == next.checksum;
        }
        return false;
    }
    database = next;
    return true;
}

int32_t find_record_in(const Database& input, const char* name) {
    if (!name || !*name) return -1;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (input.records[i].used && string_equal(input.records[i].name, name)) return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t find_record(const char* name) { return find_record_in(database, name); }

int32_t free_record() {
    for (uint32_t i = 0; i < maximum_packages; ++i) if (!database.records[i].used) return static_cast<int32_t>(i);
    return -1;
}

bool copy_text_exact(char* output, uint32_t capacity, const char* input) {
    const uint32_t size = static_cast<uint32_t>(string_length(input));
    if (!capacity || size + 1U > capacity) return false;
    memcpy(output, input, size + 1U);
    return true;
}

void set_reference(PackageReference& output, const native_package::VerifiedPackage& package, uint32_t package_size) {
    memset(&output, 0, sizeof(output));
    output.valid = 1U;
    output.payload_type = package.manifest.payload_type;
    output.dependency_count = package.manifest.dependency_count;
    output.conflict_count = package.manifest.conflict_count;
    output.package_size = package_size;
    output.payload_size = package.payload_size;
    copy_text_exact(output.version, sizeof(output.version), package.manifest.version);
    copy_text_exact(output.entrypoint, sizeof(output.entrypoint), package.manifest.entrypoint);
    memcpy(output.package_digest, package.package_digest, sizeof(output.package_digest));
    memcpy(output.payload_digest, package.payload_digest, sizeof(output.payload_digest));
    for (uint32_t i = 0; i < output.dependency_count; ++i) copy_text_exact(output.dependencies[i], sizeof(output.dependencies[i]), package.manifest.dependencies[i]);
    for (uint32_t i = 0; i < output.conflict_count; ++i) copy_text_exact(output.conflicts[i], sizeof(output.conflicts[i]), package.manifest.conflicts[i]);
}
