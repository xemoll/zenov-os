namespace security_audit {
constexpr uint16_t schema_version = 1U;
constexpr uint16_t disk_capacity = 64U;
constexpr uint32_t journal_bytes = 96U + static_cast<uint32_t>(disk_capacity) * 128U;
constexpr const char* journal_path = "/security/zenovguard.audit";

struct [[gnu::packed]] Header {
    char magic[4];
    uint16_t schema;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t capacity;
    uint32_t count;
    uint32_t next_index;
    uint32_t next_sequence;
    uint8_t anchor_hash[32];
    uint8_t head_hash[32];
    uint8_t reserved[8];
};

struct [[gnu::packed]] Record {
    uint32_t sequence;
    uint32_t tick;
    uint8_t action;
    uint8_t verdict;
    uint16_t path_length;
    char path[48];
    uint8_t digest[32];
    uint8_t record_hash[32];
    uint8_t reserved[4];
};

struct [[gnu::packed]] Journal { Header header; Record records[disk_capacity]; };
static_assert(sizeof(Header) == 96U && sizeof(Record) == 128U && sizeof(Journal) == journal_bytes);

Journal journal{};
bool ready = false;
bool replayed = false;
uint32_t persistent_appends = 0;

bool zero_bytes(const uint8_t* data, uint32_t size) {
    uint8_t combined = 0;
    for (uint32_t i = 0; i < size; ++i) combined = static_cast<uint8_t>(combined | data[i]);
    return combined == 0;
}

void store_le16(uint8_t output[2], uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
}

void store_le32(uint8_t output[4], uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
    output[2] = static_cast<uint8_t>(value >> 16U);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

void hash_record(const Record& record, const uint8_t previous_hash[32], uint8_t output[32]) {
    static constexpr uint8_t domain[16] = {'Z','E','N','O','V','-','A','U','D','I','T','-','V','1',0,0};
    uint8_t metadata[12];
    store_le32(metadata, record.sequence);
    store_le32(metadata + 4U, record.tick);
    metadata[8] = record.action;
    metadata[9] = record.verdict;
    store_le16(metadata + 10U, record.path_length);
    security_guard::Sha256Context context{};
    security_guard::sha256_init(context);
    security_guard::sha256_update(context, domain, sizeof(domain));
    security_guard::sha256_update(context, previous_hash, 32U);
    security_guard::sha256_update(context, metadata, sizeof(metadata));
    security_guard::sha256_update(context, reinterpret_cast<const uint8_t*>(record.path), sizeof(record.path));
    security_guard::sha256_update(context, record.digest, sizeof(record.digest));
    security_guard::sha256_final(context, output);
}

void initialize_empty(Journal& target) {
    memset(&target, 0, sizeof(target));
    target.header.magic[0] = 'Z'; target.header.magic[1] = 'G'; target.header.magic[2] = 'A'; target.header.magic[3] = 'L';
    target.header.schema = schema_version;
    target.header.header_size = sizeof(Header);
    target.header.record_size = sizeof(Record);
    target.header.capacity = disk_capacity;
    target.header.next_sequence = 1U;
}

bool record_valid(const Record& record) {
    if (record.action > static_cast<uint8_t>(security_guard::AuditAction::quarantine) ||
        record.verdict > static_cast<uint8_t>(security_guard::Verdict::error) ||
        !record.path_length || record.path_length >= sizeof(record.path) || record.path[record.path_length] != 0 ||
        !zero_bytes(record.reserved, sizeof(record.reserved))) return false;
    for (uint32_t i = record.path_length + 1U; i < sizeof(record.path); ++i) if (record.path[i] != 0) return false;
    return true;
}

bool verify(const Journal& candidate) {
    static constexpr char magic[4] = {'Z','G','A','L'};
    const Header& header = candidate.header;
    if (!storage::bytes_equal(header.magic, magic, sizeof(magic)) || header.schema != schema_version ||
        header.header_size != sizeof(Header) || header.record_size != sizeof(Record) || header.capacity != disk_capacity ||
        header.count > disk_capacity || header.next_index >= disk_capacity || !header.next_sequence ||
        !zero_bytes(header.reserved, sizeof(header.reserved))) return false;
    if (!header.count) {
        return header.next_index == 0 && header.next_sequence == 1U &&
            zero_bytes(header.anchor_hash, sizeof(header.anchor_hash)) && zero_bytes(header.head_hash, sizeof(header.head_hash));
    }
    if (header.next_sequence <= header.count) return false;
    if (header.count < disk_capacity) {
        if (header.next_index != header.count || !zero_bytes(header.anchor_hash, sizeof(header.anchor_hash))) return false;
    }
    const uint32_t first = header.count == disk_capacity ? header.next_index : 0U;
    const uint32_t first_sequence = header.next_sequence - header.count;
    uint8_t previous[32]; memcpy(previous, header.anchor_hash, sizeof(previous));
    for (uint32_t i = 0; i < header.count; ++i) {
        const Record& record = candidate.records[(first + i) % disk_capacity];
        if (!record_valid(record) || record.sequence != first_sequence + i) return false;
        uint8_t computed[32]; hash_record(record, previous, computed);
        if (!security_guard::digest_equal(computed, record.record_hash)) return false;
        memcpy(previous, record.record_hash, sizeof(previous));
    }
    return security_guard::digest_equal(previous, header.head_hash);
}

bool append_to(Journal& target, uint32_t tick, uint8_t action, uint8_t verdict, const char* path, const uint8_t digest[32]) {
    Header& header = target.header;
    if (header.next_sequence == 0xFFFFFFFFU || !path || !*path || !digest) return false;
    const uint32_t index = header.next_index;
    if (header.count == disk_capacity) memcpy(header.anchor_hash, target.records[index].record_hash, sizeof(header.anchor_hash));
    Record& record = target.records[index];
    memset(&record, 0, sizeof(record));
    record.sequence = header.next_sequence;
    record.tick = tick;
    record.action = action;
    record.verdict = verdict;
    if (!storage::path_copy(record.path, path, sizeof(record.path))) return false;
    record.path_length = static_cast<uint16_t>(string_length(record.path));
    memcpy(record.digest, digest, sizeof(record.digest));
    hash_record(record, header.head_hash, record.record_hash);
    if (header.count < disk_capacity) ++header.count;
    header.next_index = (index + 1U) % disk_capacity;
    ++header.next_sequence;
    memcpy(header.head_hash, record.record_hash, sizeof(header.head_hash));
    return true;
}

bool self_test() {
    Journal sample{}; initialize_empty(sample);
    uint8_t first_digest[32]{}; first_digest[0] = 0x11U;
    uint8_t second_digest[32]{}; second_digest[0] = 0x22U;
    if (!append_to(sample, 1U, static_cast<uint8_t>(security_guard::AuditAction::boot), static_cast<uint8_t>(security_guard::Verdict::trusted), "<audit-selftest-1>", first_digest) ||
        !append_to(sample, 2U, static_cast<uint8_t>(security_guard::AuditAction::scan), static_cast<uint8_t>(security_guard::Verdict::clean), "<audit-selftest-2>", second_digest) || !verify(sample)) return false;
    Journal tampered = sample;
    tampered.records[0].path[1] ^= 1;
    if (verify(tampered)) return false;
    tampered = sample;
    tampered.header.head_hash[0] ^= 1U;
    return !verify(tampered);
}

void serial_state(const char* marker) {
    serial::write(marker);
    serial::write(" count="); process::serial_u32(journal.header.count);
    serial::write(" next="); process::serial_u32(journal.header.next_sequence);
    serial::write("\r\n");
}

bool init() {
    ready = false; replayed = false; persistent_appends = 0;
    if (!self_test()) { serial::line("ZENOV_GUARD_AUDIT_SELFTEST_FAILED"); return false; }
    serial::line("ZENOV_GUARD_AUDIT_SELFTEST_OK");
    const int32_t existing = storage::find_entry(journal_path);
    if (existing < 0) {
        initialize_empty(journal);
        if (!storage::write_file(journal_path, reinterpret_cast<const uint8_t*>(&journal), sizeof(journal), false)) {
            serial::line("ZENOV_GUARD_AUDIT_CREATE_FAILED"); return false;
        }
        serial::line("ZENOV_GUARD_AUDIT_CREATED");
    } else {
        uint32_t size = 0;
        if (!storage::read_file(journal_path, reinterpret_cast<uint8_t*>(&journal), sizeof(journal), size) || size != sizeof(journal) || !verify(journal)) {
            serial::line("ZENOV_GUARD_AUDIT_INVALID"); return false;
        }
        replayed = journal.header.count != 0;
        serial_state("ZENOV_GUARD_AUDIT_REPLAY_OK");
    }
    ready = true;
    serial_state("ZENOV_GUARD_AUDIT_READY");
    return true;
}

bool append(uint32_t tick, uint8_t action, uint8_t verdict, const char* path, const uint8_t digest[32]) {
    if (!ready) return false;
    const Header previous_header = journal.header;
    const uint32_t index = journal.header.next_index;
    const Record previous_record = journal.records[index];
    if (!append_to(journal, tick, action, verdict, path, digest) ||
        !storage::write_file(journal_path, reinterpret_cast<const uint8_t*>(&journal), sizeof(journal), false)) {
        journal.header = previous_header;
        journal.records[index] = previous_record;
        ready = false;
        serial::line("ZENOV_GUARD_AUDIT_APPEND_FAILED");
        return false;
    }
    ++persistent_appends;
    return true;
}

bool verify_active() {
    if (!ready || !process::application_buffer) return false;
    uint32_t size = 0;
    if (!storage::read_file(journal_path, process::application_buffer, process::application_buffer_bytes, size) || size != sizeof(Journal)) return false;
    const auto* disk = reinterpret_cast<const Journal*>(process::application_buffer);
    if (!verify(*disk) || disk->header.count != journal.header.count || disk->header.next_sequence != journal.header.next_sequence ||
        !security_guard::digest_equal(disk->header.head_hash, journal.header.head_hash)) return false;
    serial_state("ZENOV_GUARD_AUDIT_VERIFY_OK");
    return true;
}

void print_hash_prefix(const uint8_t hash[32]) {
    static constexpr char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 8U; ++i) { console::put(hex[hash[i] >> 4U]); console::put(hex[hash[i] & 0x0FU]); }
}

void print_status() {
    console::write("  Audit journal "); console::line(ready ? "persistent / hash-chained / COW" : "invalid / execution lock required");
    console::write("  Audit records "); console::unsigned_dec(journal.header.count); console::put('\n');
    console::write("  Next sequence "); console::unsigned_dec(journal.header.next_sequence); console::put('\n');
    console::write("  Head hash     "); print_hash_prefix(journal.header.head_hash); console::put('\n');
    console::write("  Boot replay   "); console::line(replayed ? "verified existing journal" : "empty factory journal");
}

void print() {
    if (!ready) { console::warning("Persistent audit journal is unavailable."); return; }
    if (!journal.header.count) { console::line("  <no persistent security events>"); return; }
    const uint32_t first = journal.header.count == disk_capacity ? journal.header.next_index : 0U;
    for (uint32_t i = 0; i < journal.header.count; ++i) {
        const Record& record = journal.records[(first + i) % disk_capacity];
        console::write("  #"); console::unsigned_dec(record.sequence); console::write("  "); console::unsigned_dec(record.tick);
        console::write("  "); console::write(security_guard::action_name(static_cast<security_guard::AuditAction>(record.action)));
        console::write("  "); console::write(security_guard::verdict_name(static_cast<security_guard::Verdict>(record.verdict)));
        console::write("  "); console::line(record.path);
    }
}
} // namespace security_audit
