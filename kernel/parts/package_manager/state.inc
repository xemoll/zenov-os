constexpr const char* state_path = "/var/lib/zenpkg/state.v1";
constexpr uint32_t maximum_packages = 8U;

struct [[gnu::packed]] PackageReference {
    uint8_t valid;
    uint8_t payload_type;
    uint16_t reserved;
    char version[16];
    char entrypoint[48];
    uint8_t package_digest[32];
    uint8_t payload_digest[32];
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

static_assert(sizeof(PackageReference) == 132U);
static_assert(sizeof(PackageRecord) == 300U);
static_assert(sizeof(Database) == 2424U);

Database database{};
uint8_t* package_buffer = nullptr;
bool initialized = false;

uint32_t database_checksum(const Database& input) {
    Database copy = input;
    copy.checksum = 0;
    return storage::fnv1a(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool digest_equal(const uint8_t left[32], const uint8_t right[32]) {
    return crypto::digest_equal(left, right);
}

uint32_t record_count(const Database& input) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < maximum_packages; ++i) if (input.records[i].used) ++count;
    return count;
}

bool reference_valid(const PackageReference& reference) {
    if (!reference.valid) return reference.payload_type == 0 && reference.version[0] == 0 && reference.entrypoint[0] == 0;
    if (reference.payload_type != native_package::payload_zex1 && reference.payload_type != native_package::payload_elf32) return false;
    if (!reference.version[0] || !reference.entrypoint[0]) return false;
    if (!string_starts_with(reference.entrypoint, "/data/apps/pkg-")) return false;
    return true;
}

bool database_valid(const Database& input) {
    const char expected[8] = {'Z','P','K','D','B','1',0,0};
    if (!storage::bytes_equal(input.magic, expected, sizeof(expected)) || input.version != 1U) return false;
    if (input.count != record_count(input) || input.checksum != database_checksum(input)) return false;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        const PackageRecord& record = input.records[i];
        if (!record.used) continue;
        if (!record.name[0] || !reference_valid(record.active) || !record.active.valid || !reference_valid(record.previous)) return false;
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
    output.generation = 0;
    output.count = 0;
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
    if (!storage::read_file(state_path, reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded), size)) return false;
    if (size != sizeof(loaded) || !database_valid(loaded)) return false;
    database = loaded;
    return true;
}

bool persist_database(Database& next) {
    next.count = record_count(next);
    next.generation = database.generation + 1U;
    next.checksum = database_checksum(next);
    if (!database_valid(next)) return false;
    if (!storage::package_write_file(state_path, reinterpret_cast<const uint8_t*>(&next), sizeof(next), false)) return false;
    if (!storage::sync_metadata()) {
        Database recovered{};
        uint32_t recovered_size = 0;
        if (storage::read_file(state_path, reinterpret_cast<uint8_t*>(&recovered), sizeof(recovered), recovered_size) &&
            recovered_size == sizeof(recovered) && database_valid(recovered)) {
            database = recovered;
            return recovered.generation == next.generation && recovered.checksum == next.checksum;
        }
        return false;
    }
    database = next;
    return true;
}

int32_t find_record(const char* name) {
    if (!name || !*name) return -1;
    for (uint32_t i = 0; i < maximum_packages; ++i) {
        if (database.records[i].used && string_equal(database.records[i].name, name)) return static_cast<int32_t>(i);
    }
    return -1;
}

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

void set_reference(PackageReference& output, const native_package::VerifiedPackage& package) {
    memset(&output, 0, sizeof(output));
    output.valid = 1U;
    output.payload_type = package.manifest.payload_type;
    copy_text_exact(output.version, sizeof(output.version), package.manifest.version);
    copy_text_exact(output.entrypoint, sizeof(output.entrypoint), package.manifest.entrypoint);
    memcpy(output.package_digest, package.package_digest, sizeof(output.package_digest));
    crypto::sha256(package.payload, package.payload_size, output.payload_digest);
}

