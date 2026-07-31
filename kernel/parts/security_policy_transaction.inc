namespace security_policy_transaction {
constexpr uint32_t workspace_bytes = 16U * 1024U;
constexpr uint32_t version_backup_bytes = 16U;
constexpr uint32_t journal_schema = 1U;
constexpr uint8_t journal_state_prepared = 1U;
constexpr uint32_t journal_path_bytes = 48U;
constexpr const char* journal_path = "/security/policy-transaction.journal";

enum class Domain : uint8_t { none = 0U, zgdb = 1U, zcap = 2U, zmid = 3U };

struct Descriptor {
    Domain domain;
    const char* live_path;
    const char* version_path;
    const char* auxiliary_path;
    uint32_t policy_capacity;
    uint32_t auxiliary_capacity;
};

constexpr Descriptor descriptors[] = {
    {Domain::zgdb, "/security/zenovguard.zgdb", "/security/zenovguard.version", nullptr, 16U * 1024U, 0U},
    {Domain::zcap, "/security/syscall-capabilities.zcap", "/security/syscall-capabilities.version", nullptr, 8U * 1024U, 0U},
    {Domain::zmid, "/security/zenovguard-intelligence.zmid", "/security/zenovguard-intelligence.version",
        "/security/zenovguard.audit", 16U * 1024U, 16U * 1024U},
};

struct [[gnu::packed]] JournalHeader {
    char magic[4];
    uint16_t schema;
    uint16_t header_size;
    uint8_t state;
    uint8_t domain;
    uint16_t flags;
    uint32_t policy_size;
    uint32_t version_size;
    uint32_t auxiliary_size;
    uint32_t previous_version;
    uint32_t payload_size;
    uint8_t digest[32];
    char live_path[journal_path_bytes];
    char version_path[journal_path_bytes];
    char auxiliary_path[journal_path_bytes];
    uint8_t reserved[16];
};
static_assert(sizeof(JournalHeader) == 224U);

#ifdef SECURITY_POLICY_TRANSACTION_HOST_TEST
alignas(16) uint8_t host_rollback_data[workspace_bytes]{};
uint8_t* rollback_data = host_rollback_data;
#else
constexpr uintptr_t workspace_address = supervisor_layout::policy_transaction;
static_assert(workspace_bytes == supervisor_layout::policy_transaction_bytes);
static_assert(workspace_address >= supervisor_layout::pmm_bitmap + supervisor_layout::pmm_bitmap_bytes);
static_assert(workspace_address + workspace_bytes <= supervisor_layout::user_base);
uint8_t* rollback_data = reinterpret_cast<uint8_t*>(workspace_address);
#endif
uint8_t version_backup[version_backup_bytes]{};
uint32_t version_backup_size = 0U;
bool busy = false;
Domain recovered_domain = Domain::none;

uint32_t bounded_text_length(const char* value, uint32_t capacity) {
    if (!value) return 0U;
    uint32_t length = 0U;
    while (length < capacity && value[length]) ++length;
    return length;
}

bool text_equal(const char* left, const char* right) {
    if (!left) return !right || !*right;
    if (!right) return !*left;
    for (uint32_t index = 0U; index < journal_path_bytes; ++index) {
        if (left[index] != right[index]) return false;
        if (!left[index]) return true;
    }
    return false;
}

bool fixed_text_equal(const char* encoded, uint32_t capacity, const char* expected) {
    if (!encoded || !capacity) return false;
    const uint32_t expected_length = bounded_text_length(expected, capacity);
    if (expected && expected_length == capacity) return false;
    if (!expected) {
        for (uint32_t index = 0U; index < capacity; ++index) if (encoded[index] != 0) return false;
        return true;
    }
    for (uint32_t index = 0U; index < expected_length; ++index) {
        if (encoded[index] != expected[index]) return false;
    }
    if (encoded[expected_length] != 0) return false;
    for (uint32_t index = expected_length + 1U; index < capacity; ++index) {
        if (encoded[index] != 0) return false;
    }
    return true;
}

bool copy_text(char* output, uint32_t capacity, const char* input) {
    if (!output || !capacity) return false;
    memset(output, 0, capacity);
    if (!input) return true;
    const uint32_t length = bounded_text_length(input, capacity);
    if (length == capacity) return false;
    if (length) memcpy(output, input, length);
    return true;
}

const Descriptor* descriptor_for_live(const char* live_path) {
    for (const Descriptor& descriptor : descriptors) {
        if (text_equal(live_path, descriptor.live_path)) return &descriptor;
    }
    return nullptr;
}

const Descriptor* descriptor_for_domain(Domain domain) {
    for (const Descriptor& descriptor : descriptors) if (descriptor.domain == domain) return &descriptor;
    return nullptr;
}

const char* domain_name(Domain domain) {
    switch (domain) {
        case Domain::zgdb: return "ZGDB";
        case Domain::zcap: return "ZCAP";
        case Domain::zmid: return "ZMID";
        default: return "NONE";
    }
}

bool prepare_uncertain() { return busy; }

void wipe() {
    volatile uint8_t* output = rollback_data;
    for (uint32_t i = 0U; i < workspace_bytes; ++i) output[i] = 0U;
    volatile uint8_t* version_output = version_backup;
    for (uint32_t i = 0U; i < version_backup_bytes; ++i) version_output[i] = 0U;
    version_backup_size = 0U;
}

bool version_matches(const char* version_path, uint32_t expected) {
    uint8_t data[version_backup_bytes]{};
    uint32_t size = 0U, parsed = 0U;
    return version_path && *version_path && expected &&
        storage::read_file(version_path, data, sizeof(data), size) &&
        security_policy_format::parse_nonzero_decimal_u32(data, size, parsed) && parsed == expected;
}

bool digest_valid(uint8_t* journal, uint32_t size) {
    if (!journal || size < sizeof(JournalHeader)) return false;
    auto* header = reinterpret_cast<JournalHeader*>(journal);
    uint8_t expected[32]{};
    uint8_t actual[32]{};
    memcpy(expected, header->digest, sizeof(expected));
    memset(header->digest, 0, sizeof(header->digest));
    security_guard::sha256(journal, size, actual);
    memcpy(header->digest, expected, sizeof(expected));
    return storage::bytes_equal(expected, actual, sizeof(expected));
}

bool validate_journal(uint8_t* journal, uint32_t size, const Descriptor*& descriptor,
        const uint8_t*& policy, const uint8_t*& version, const uint8_t*& auxiliary,
        uint32_t& previous_version) {
    descriptor = nullptr;
    policy = version = auxiliary = nullptr;
    previous_version = 0U;
    if (!journal || size < sizeof(JournalHeader)) return false;
    auto* header = reinterpret_cast<JournalHeader*>(journal);
    const char magic[4] = {'Z','P','T','J'};
    if (!storage::bytes_equal(header->magic, magic, sizeof(magic)) ||
        header->schema != journal_schema || header->header_size != sizeof(JournalHeader) ||
        header->state != journal_state_prepared || header->flags != 0U) return false;
    for (uint8_t byte : header->reserved) if (byte != 0U) return false;

    descriptor = descriptor_for_domain(static_cast<Domain>(header->domain));
    if (!descriptor ||
        !fixed_text_equal(header->live_path, sizeof(header->live_path), descriptor->live_path) ||
        !fixed_text_equal(header->version_path, sizeof(header->version_path), descriptor->version_path) ||
        !fixed_text_equal(header->auxiliary_path, sizeof(header->auxiliary_path), descriptor->auxiliary_path)) return false;
    if (!header->policy_size || header->policy_size > descriptor->policy_capacity ||
        header->version_size < 2U || header->version_size > 11U || !header->previous_version) return false;
    if (descriptor->auxiliary_path) {
        if (!header->auxiliary_size || header->auxiliary_size > descriptor->auxiliary_capacity) return false;
    } else if (header->auxiliary_size != 0U) return false;
    const uint32_t payload = header->policy_size + header->version_size + header->auxiliary_size;
    if (header->payload_size != payload || payload > process::application_buffer_bytes - sizeof(JournalHeader) ||
        size != sizeof(JournalHeader) + payload || !digest_valid(journal, size)) return false;

    policy = journal + sizeof(JournalHeader);
    version = policy + header->policy_size;
    auxiliary = version + header->version_size;
    uint32_t parsed = 0U;
    if (!security_policy_format::parse_nonzero_decimal_u32(version, header->version_size, parsed) ||
        parsed != header->previous_version) return false;
    previous_version = parsed;
    return true;
}

bool read_journal(uint32_t& size) {
    size = 0U;
    return process::application_buffer &&
        storage::read_file(journal_path, process::application_buffer,
            process::application_buffer_bytes, size);
}

bool clear_journal() {
    if (!storage::write_file(journal_path, nullptr, 0U, false)) return false;
    uint32_t size = 0U;
    if (!read_journal(size) || size != 0U) return false;
    return storage::sync_metadata();
}

bool persist_journal(const Descriptor& descriptor, uint32_t policy_size,
        uint32_t previous_version) {
    if (!process::application_buffer || !policy_size || !version_backup_size) return false;
    uint8_t* journal = process::application_buffer;
    const uint32_t auxiliary_offset = sizeof(JournalHeader) + policy_size + version_backup_size;
    uint32_t auxiliary_size = 0U;
    if (descriptor.auxiliary_path) {
        if (!storage::read_file(descriptor.auxiliary_path, journal + auxiliary_offset,
                descriptor.auxiliary_capacity, auxiliary_size) || !auxiliary_size) return false;
    }
    const uint32_t total = auxiliary_offset + auxiliary_size;
    if (total > process::application_buffer_bytes) return false;

    JournalHeader header{};
    header.magic[0] = 'Z'; header.magic[1] = 'P'; header.magic[2] = 'T'; header.magic[3] = 'J';
    header.schema = journal_schema;
    header.header_size = sizeof(JournalHeader);
    header.state = journal_state_prepared;
    header.domain = static_cast<uint8_t>(descriptor.domain);
    header.policy_size = policy_size;
    header.version_size = version_backup_size;
    header.auxiliary_size = auxiliary_size;
    header.previous_version = previous_version;
    header.payload_size = policy_size + version_backup_size + auxiliary_size;
    if (!copy_text(header.live_path, sizeof(header.live_path), descriptor.live_path) ||
        !copy_text(header.version_path, sizeof(header.version_path), descriptor.version_path) ||
        !copy_text(header.auxiliary_path, sizeof(header.auxiliary_path), descriptor.auxiliary_path)) return false;

    memmove(journal + sizeof(JournalHeader), rollback_data, policy_size);
    memcpy(journal + sizeof(JournalHeader) + policy_size, version_backup, version_backup_size);
    memcpy(journal, &header, sizeof(header));
    auto* journal_header = reinterpret_cast<JournalHeader*>(journal);
    security_guard::sha256(journal, total, journal_header->digest);
    uint8_t expected_digest[32]{};
    memcpy(expected_digest, journal_header->digest, sizeof(expected_digest));

    if (!storage::write_file(journal_path, journal, total, false)) return false;
    uint32_t readback_size = 0U;
    if (!read_journal(readback_size) || readback_size != total) return false;
    const Descriptor* checked_descriptor = nullptr;
    const uint8_t *policy = nullptr, *version = nullptr, *auxiliary = nullptr;
    uint32_t checked_version = 0U;
    if (!validate_journal(journal, readback_size, checked_descriptor, policy, version,
            auxiliary, checked_version) || checked_descriptor != &descriptor || checked_version != previous_version ||
        !storage::bytes_equal(reinterpret_cast<const JournalHeader*>(journal)->digest,
            expected_digest, sizeof(expected_digest))) return false;
    return storage::sync_metadata();
}

bool acquire(const char* live_path, uint32_t capacity, uint32_t& size) {
    size = 0U;
    if (busy || !live_path || !*live_path || !capacity || capacity > workspace_bytes) return false;
    const Descriptor* descriptor = descriptor_for_live(live_path);
    if (!descriptor || capacity > descriptor->policy_capacity) return false;
    busy = true;
    recovered_domain = Domain::none;
    if (!storage::read_file(live_path, rollback_data, capacity, size) || !size || size > capacity ||
        !storage::read_file(descriptor->version_path, version_backup,
            sizeof(version_backup), version_backup_size)) {
        wipe(); busy = false; size = 0U; return false;
    }
    uint32_t previous_version = 0U;
    if (!security_policy_format::parse_nonzero_decimal_u32(version_backup, version_backup_size,
            previous_version) || !persist_journal(*descriptor, size, previous_version)) {
        if (clear_journal()) { wipe(); busy = false; }
        size = 0U;
        return false;
    }
    return true;
}

[[maybe_unused]] bool restore_verified(const char* live_path, uint32_t size, uint8_t* readback, uint32_t capacity) {
    if (!busy || !live_path || !*live_path || !size || size > workspace_bytes || !readback || capacity < size) return false;
    if (!storage::write_file(live_path, rollback_data, size, false)) return false;
    uint32_t actual = 0U;
    if (!storage::read_file(live_path, readback, capacity, actual) || actual != size) return false;
    return storage::bytes_equal(readback, rollback_data, size);
}

bool restore_hot_journal() {
    uint32_t size = 0U;
    if (!read_journal(size) || size == 0U) return false;
    const Descriptor* descriptor = nullptr;
    const uint8_t *policy = nullptr, *version = nullptr, *auxiliary = nullptr;
    uint32_t previous_version = 0U;
    if (!validate_journal(process::application_buffer, size, descriptor, policy, version,
            auxiliary, previous_version)) return false;
    const auto* header = reinterpret_cast<const JournalHeader*>(process::application_buffer);

    if (!storage::write_file(descriptor->live_path, policy, header->policy_size, false)) return false;
    uint32_t actual = 0U;
    if (!storage::read_file(descriptor->live_path, rollback_data, descriptor->policy_capacity, actual) ||
        actual != header->policy_size || !storage::bytes_equal(rollback_data, policy, actual)) return false;

    if (!storage::write_file(descriptor->version_path, version, header->version_size, false)) return false;
    actual = 0U;
    if (!storage::read_file(descriptor->version_path, version_backup, sizeof(version_backup), actual) ||
        actual != header->version_size || !storage::bytes_equal(version_backup, version, actual) ||
        !version_matches(descriptor->version_path, previous_version)) return false;

    if (descriptor->auxiliary_path) {
        if (!storage::write_file(descriptor->auxiliary_path, auxiliary, header->auxiliary_size, false)) return false;
        actual = 0U;
        if (!storage::read_file(descriptor->auxiliary_path, rollback_data,
                descriptor->auxiliary_capacity, actual) || actual != header->auxiliary_size ||
            !storage::bytes_equal(rollback_data, auxiliary, actual)) return false;
    }
    if (!storage::sync_metadata()) return false;
    recovered_domain = descriptor->domain;
    if (!clear_journal()) return false;
    wipe();
    busy = false;
    return true;
}

bool recover_pending() {
    if (busy) return false;
    recovered_domain = Domain::none;
    uint32_t size = 0U;
    if (!read_journal(size)) return false;
    if (size == 0U) return true;
    return restore_hot_journal();
}

bool rollback() {
    if (!busy) return false;
    return restore_hot_journal();
}

bool commit() {
    if (!busy) return false;
    if (!clear_journal()) return false;
    wipe();
    busy = false;
    return true;
}

bool discard() { return commit(); }
[[maybe_unused]] bool release() { return discard(); }
} // namespace security_policy_transaction
