namespace security_guard {
static_assert(sizeof(unsigned long long) == 8);

constexpr uint32_t sha256_bytes = 32U;
constexpr uint32_t audit_capacity = 32U;

enum class Verdict : uint8_t { clean, trusted, untrusted, suspicious, infected, error };
enum class AuditAction : uint8_t { boot, scan, execute, quarantine, write_block, intelligence_update };

struct ScanResult {
    Verdict verdict = Verdict::error;
    char path[48]{};
    char signature[48]{};
    uint8_t digest[sha256_bytes]{};
    uint32_t size = 0;
    uint8_t policy_action = 0U;
    bool executable = false;
};

struct AuditRecord {
    uint32_t tick = 0;
    AuditAction action = AuditAction::scan;
    Verdict verdict = Verdict::error;
    char path[48]{};
    uint32_t digest_prefix = 0;
};

struct TrustedExecutable { const char* path; uint8_t digest[sha256_bytes]; };
static constexpr TrustedExecutable trusted_executables[] = {
    {"/apps/hello.zex", {0x2b,0x7b,0xa0,0x11,0x4d,0x52,0x28,0x82,0x5b,0x30,0xac,0xa3,0x0e,0x0e,0x97,0x8f,0x2f,0xaf,0x9b,0x79,0x8c,0xf7,0xf5,0x49,0x47,0x42,0xd7,0xa1,0xd3,0x30,0x95,0x6a}},
    {"/apps/fileio.elf", {0x5a,0xcc,0x70,0xa7,0x8b,0xd8,0x30,0xcd,0x7b,0x04,0x79,0x9b,0xf3,0xc2,0xbc,0x22,0x90,0x5a,0xc5,0x30,0x70,0xdb,0x00,0xb5,0x35,0x68,0x7c,0x8b,0x70,0x3a,0x93,0x4e}},
    {"/apps/args.elf", {0x4e,0xef,0x2c,0x49,0xa2,0xec,0x2c,0xf5,0x9a,0xda,0x1d,0x0b,0x0b,0x78,0x10,0x52,0xb6,0xbf,0x01,0x44,0x1a,0x31,0x6b,0xb9,0xeb,0x39,0x9a,0x23,0xee,0xdb,0x07,0x41}},
    {"/apps/console.elf", {0xd1,0x64,0xc2,0x3e,0x8a,0xcb,0x2b,0xcf,0xb5,0xab,0xa4,0x43,0x57,0xd4,0xf4,0xa4,0xd2,0x23,0x57,0x7c,0x4a,0x92,0x04,0x7c,0xe5,0x7e,0x3a,0x16,0x3d,0x40,0x71,0x57}},
    {"/apps/protect.elf", {0x1a,0xad,0xb7,0xd6,0xaf,0x1a,0x1f,0xef,0x13,0x1d,0x8c,0x97,0x43,0x8e,0xac,0x7c,0x29,0xfb,0x43,0xf0,0x07,0x50,0x6e,0xa3,0x19,0xa7,0x1c,0xfe,0x4d,0x44,0xf2,0xb5}},
    {"/apps/kaccess.elf", {0xac,0xf6,0x67,0xbf,0xe6,0x4b,0x37,0xef,0x1c,0xe1,0x13,0xc9,0x34,0x02,0xc0,0x56,0x57,0x79,0xf7,0xd9,0xa7,0x89,0xec,0x8c,0xd6,0x1c,0x4b,0x53,0xd1,0xee,0xe7,0x34}},
    {"/apps/zenovapp.zex", {0x9e,0x17,0x33,0xaf,0x56,0xa5,0x3a,0xe3,0x10,0x55,0xb4,0x48,0xf7,0x62,0x81,0x5b,0xa7,0xa5,0xe1,0xa7,0x2b,0xe5,0x43,0xae,0xf3,0x25,0xbd,0x4e,0xa3,0x6e,0x0a,0xd5}},
};



struct Sha256Context {
    uint32_t state[8];
    unsigned long long total_bytes;
    uint8_t block[64];
    uint32_t used;
};

static constexpr uint32_t sha256_constants[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U,
};

AuditRecord audit_log[audit_capacity]{};
uint32_t audit_next = 0, audit_count = 0;
uint32_t scans = 0, blocked = 0, quarantined = 0, trusted_launches = 0;
uint32_t write_scans = 0, write_blocked = 0, audit_only_findings = 0;
alignas(16) uint8_t write_scan_storage[process::application_buffer_bytes]{};
uint8_t* write_scan_buffer = write_scan_storage;
bool self_test_passed = false, baseline_verified = false, enforcement_ready = false;

uint32_t rotate_right(uint32_t value, uint32_t count) { return (value >> count) | (value << (32U - count)); }
uint32_t load_be32(const uint8_t* input) {
    return (static_cast<uint32_t>(input[0]) << 24U) | (static_cast<uint32_t>(input[1]) << 16U) |
        (static_cast<uint32_t>(input[2]) << 8U) | static_cast<uint32_t>(input[3]);
}
void store_be32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value >> 24U); output[1] = static_cast<uint8_t>(value >> 16U);
    output[2] = static_cast<uint8_t>(value >> 8U); output[3] = static_cast<uint8_t>(value);
}
void sha256_transform(Sha256Context& context, const uint8_t* block) {
    uint32_t words[64];
    for (uint32_t i = 0; i < 16U; ++i) words[i] = load_be32(block + i * 4U);
    for (uint32_t i = 16U; i < 64U; ++i) {
        const uint32_t s0 = rotate_right(words[i - 15U], 7U) ^ rotate_right(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
        const uint32_t s1 = rotate_right(words[i - 2U], 17U) ^ rotate_right(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }
    uint32_t a = context.state[0], b = context.state[1], c = context.state[2], d = context.state[3];
    uint32_t e = context.state[4], f = context.state[5], g = context.state[6], h = context.state[7];
    for (uint32_t i = 0; i < 64U; ++i) {
        const uint32_t sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + sum1 + choice + sha256_constants[i] + words[i];
        const uint32_t sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    context.state[0] += a; context.state[1] += b; context.state[2] += c; context.state[3] += d;
    context.state[4] += e; context.state[5] += f; context.state[6] += g; context.state[7] += h;
}
void sha256_init(Sha256Context& context) {
    context.state[0] = 0x6a09e667U; context.state[1] = 0xbb67ae85U; context.state[2] = 0x3c6ef372U; context.state[3] = 0xa54ff53aU;
    context.state[4] = 0x510e527fU; context.state[5] = 0x9b05688cU; context.state[6] = 0x1f83d9abU; context.state[7] = 0x5be0cd19U;
    context.total_bytes = 0; context.used = 0;
}
void sha256_update(Sha256Context& context, const uint8_t* data, uint32_t size) {
    if (!data && size) return;
    context.total_bytes += size;
    for (uint32_t i = 0; i < size; ++i) {
        context.block[context.used++] = data[i];
        if (context.used == 64U) { sha256_transform(context, context.block); context.used = 0; }
    }
}
void sha256_final(Sha256Context& context, uint8_t output[sha256_bytes]) {
    const unsigned long long bit_length = context.total_bytes * 8ULL;
    context.block[context.used++] = 0x80U;
    if (context.used > 56U) {
        while (context.used < 64U) context.block[context.used++] = 0;
        sha256_transform(context, context.block); context.used = 0;
    }
    while (context.used < 56U) context.block[context.used++] = 0;
    for (uint32_t i = 0; i < 8U; ++i) context.block[63U - i] = static_cast<uint8_t>(bit_length >> (i * 8U));
    sha256_transform(context, context.block);
    for (uint32_t i = 0; i < 8U; ++i) store_be32(output + i * 4U, context.state[i]);
}
void sha256(const uint8_t* data, uint32_t size, uint8_t output[sha256_bytes]) {
    Sha256Context context{}; sha256_init(context); sha256_update(context, data, size); sha256_final(context, output);
}
bool digest_equal(const uint8_t* left, const uint8_t* right) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < sha256_bytes; ++i) difference = static_cast<uint8_t>(difference | (left[i] ^ right[i]));
    return difference == 0;
}
uint32_t digest_prefix(const uint8_t digest[sha256_bytes]) {
    return (static_cast<uint32_t>(digest[0]) << 24U) | (static_cast<uint32_t>(digest[1]) << 16U) |
        (static_cast<uint32_t>(digest[2]) << 8U) | digest[3];
}

const char* verdict_name(Verdict verdict) {
    switch (verdict) {
        case Verdict::clean: return "CLEAN"; case Verdict::trusted: return "TRUSTED"; case Verdict::untrusted: return "UNTRUSTED";
        case Verdict::suspicious: return "SUSPICIOUS"; case Verdict::infected: return "INFECTED"; default: return "ERROR";
    }
}
const char* action_name(AuditAction action) {
    switch (action) {
        case AuditAction::boot: return "BOOT";
        case AuditAction::scan: return "SCAN";
        case AuditAction::execute: return "EXEC";
        case AuditAction::quarantine: return "QUARANTINE";
        case AuditAction::write_block: return "WRITE-BLOCK";
        case AuditAction::intelligence_update: return "INTELLIGENCE-UPDATE";
        default: return "UNKNOWN";
    }
}
bool record(AuditAction action, const ScanResult& result) {
    AuditRecord& entry = audit_log[audit_next];
    entry.tick = timer_ticks; entry.action = action; entry.verdict = result.verdict; entry.digest_prefix = digest_prefix(result.digest);
    storage::path_copy(entry.path, result.path[0] ? result.path : "<memory>", sizeof(entry.path));
    audit_next = (audit_next + 1U) % audit_capacity;
    if (audit_count < audit_capacity) ++audit_count;
    if (!security_audit::append(entry.tick, static_cast<uint8_t>(entry.action), static_cast<uint8_t>(entry.verdict), entry.path, result.digest)) {
        enforcement_ready = false;
        serial::line("ZENOV_GUARD_AUDIT_ENFORCEMENT_LOCK");
        return false;
    }
    return true;
}

bool trusted_digest(const char* normalized_path, const uint8_t digest[sha256_bytes]) {
    if (!normalized_path) return false;
    for (const auto& trusted : trusted_executables) if (string_equal(normalized_path, trusted.path) && digest_equal(digest, trusted.digest)) return true;
    return false;
}
bool starts_with_quarantine(const char* path) { return path && string_starts_with(path, "/quarantine/"); }

bool zex_valid(const uint8_t* data, uint32_t size) {
    if (size < sizeof(process::ZexHeader)) return false;
    const auto* header = reinterpret_cast<const process::ZexHeader*>(data);
    const char expected[4] = {'Z','E','X','1'};
    return storage::bytes_equal(header->magic, expected, 4) && header->version == 1U && header->header_size == sizeof(process::ZexHeader) &&
        header->image_size && header->image_size <= size - header->header_size && header->image_size + header->bss_size <= process::stack_base &&
        header->entry_offset < header->image_size && header->stack_size <= process::stack_bytes &&
        process::checksum(data + header->header_size, header->image_size) == header->checksum;
}
bool elf_valid(const uint8_t* data, uint32_t size) {
    if (size < sizeof(process::Elf32Header)) return false;
    const auto* header = reinterpret_cast<const process::Elf32Header*>(data);
    if (header->ident[0] != 0x7FU || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 1U || header->ident[5] != 1U || header->type != 2U || header->machine != 3U || header->version != 1U ||
        header->header_size != sizeof(process::Elf32Header) || header->program_entry_size != sizeof(process::Elf32ProgramHeader) ||
        !header->program_count || header->program_offset > size || header->program_count > (size - header->program_offset) / sizeof(process::Elf32ProgramHeader)) return false;
    bool loadable = false, entry_executable = false;
    for (uint32_t i = 0; i < header->program_count; ++i) {
        const auto* segment = reinterpret_cast<const process::Elf32ProgramHeader*>(data + header->program_offset + i * sizeof(process::Elf32ProgramHeader));
        if (segment->type != 1U) continue;
        if ((segment->flags & 0x3U) == 0x3U || (segment->flags & ~7U)) return false;
        if (!segment->memory_size) { if (segment->file_size) return false; continue; }
        if (segment->file_size > segment->memory_size || segment->offset > size || segment->file_size > size - segment->offset ||
            segment->virtual_address >= process::stack_base || segment->memory_size > process::stack_base - segment->virtual_address) return false;
        if ((segment->flags & 1U) && header->entry >= segment->virtual_address && header->entry < segment->virtual_address + segment->memory_size) entry_executable = true;
        loadable = true;
    }
    return loadable && entry_executable;
}

ScanResult scan_memory(const char* path, const uint8_t* data, uint32_t size, bool appraise_executable = true) {
    ScanResult result{}; result.size = size;
    if (path && *path) {
        char normalized[48];
        if (storage::normalize_path(path, normalized)) storage::path_copy(result.path, normalized, sizeof(result.path));
        else storage::path_copy(result.path, path, sizeof(result.path));
    } else storage::path_copy(result.path, "<memory>", sizeof(result.path));
    if (!data && size) { result.verdict = Verdict::error; return result; }
    sha256(data, size, result.digest); ++scans;
    if (zmid::classify(result.path, data, size, result.digest, result)) return result;
    const bool zex = size >= 4U && data[0] == 'Z' && data[1] == 'E' && data[2] == 'X' && data[3] == '1';
    const bool elf = size >= 4U && data[0] == 0x7FU && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
    result.executable = zex || elf;
    if ((zex && !zex_valid(data, size)) || (elf && !elf_valid(data, size))) {
        result.verdict = Verdict::suspicious; storage::path_copy(result.signature, "Malformed.Executable", sizeof(result.signature)); return result;
    }
    if (result.executable && appraise_executable) result.verdict = trusted_digest(result.path, result.digest) ? Verdict::trusted : Verdict::untrusted;
    else result.verdict = Verdict::clean;
    return result;
}

bool scan_path(const char* path, ScanResult& result) {
    if (!path || !*path || !write_scan_buffer) return false;
    uint32_t size = 0;
    if (!storage::read_file(path, write_scan_buffer, process::application_buffer_bytes, size)) return false;
    result = scan_memory(path, write_scan_buffer, size, true);
    return record(AuditAction::scan, result);
}

void serial_decision(const char* marker, const ScanResult& result) {
    serial::write(marker); serial::write(" path="); serial::write(result.path); serial::write(" verdict="); serial::write(verdict_name(result.verdict));
    if (result.signature[0]) { serial::write(" signature="); serial::write(result.signature); }
    serial::write(" digest=");
    static constexpr char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 8U; ++i) { serial::put(hex[result.digest[i] >> 4U]); serial::put(hex[result.digest[i] & 0x0FU]); }
    serial::write("\r\n");
}

bool ensure_quarantine_directory() {
    const int32_t existing = storage::find_entry("/quarantine");
    if (existing >= 0) return storage::zfs_entries[existing].type == storage::zfs_type_directory;
    return storage::make_directory("/quarantine");
}
void build_quarantine_path(const uint8_t digest[sha256_bytes], char output[48]) {
    static constexpr char hex[] = "0123456789abcdef";
    storage::path_copy(output, "/quarantine/q-", 48);
    uint32_t position = static_cast<uint32_t>(string_length(output));
    for (uint32_t i = 0; i < 6U; ++i) { output[position++] = hex[digest[i] >> 4U]; output[position++] = hex[digest[i] & 0x0FU]; }
    output[position++] = '.'; output[position++] = 'q'; output[position++] = 't'; output[position++] = 'n'; output[position] = 0;
}
bool quarantine_path(const char* path) {
    ScanResult result{};
    if (!scan_path(path, result) || (result.verdict != Verdict::infected && result.verdict != Verdict::suspicious && result.verdict != Verdict::untrusted)) return false;
    char normalized[48];
    if (!storage::normalize_path(path, normalized) || starts_with_quarantine(normalized) || !ensure_quarantine_directory()) return false;
    char destination[48]; build_quarantine_path(result.digest, destination);
    char metadata_path[48]{};
    storage::path_copy(metadata_path, destination, sizeof(metadata_path));
    const uint32_t length = static_cast<uint32_t>(string_length(metadata_path));
    if (length + 5U >= sizeof(metadata_path)) return false;
    metadata_path[length] = '.'; metadata_path[length + 1U] = 'm'; metadata_path[length + 2U] = 'e'; metadata_path[length + 3U] = 't'; metadata_path[length + 4U] = 'a'; metadata_path[length + 5U] = 0;
    if (!storage::quarantine_rename(normalized, destination)) return false;
    char metadata[192]{};
    uint32_t used = 0U;
    auto append = [&](const char* text) {
        while (*text && used + 1U < sizeof(metadata)) metadata[used++] = *text++;
    };
    append("ZQMD1\noriginal="); append(normalized); append("\nsignature="); append(result.signature[0] ? result.signature : "manual"); append("\nverdict="); append(verdict_name(result.verdict)); append("\n");
    if (!storage::quarantine_write_metadata(metadata_path, reinterpret_cast<const uint8_t*>(metadata), used)) {
        if (!storage::quarantine_restore(destination, normalized)) serial::line("ZENOV_GUARD_QUARANTINE_ROLLBACK_FAILED stage=metadata");
        return false;
    }
    if (!record(AuditAction::quarantine, result)) {
        const bool metadata_removed = storage::quarantine_remove(metadata_path);
        const bool payload_restored = storage::quarantine_restore(destination, normalized);
        if (!metadata_removed || !payload_restored) serial::line("ZENOV_GUARD_QUARANTINE_ROLLBACK_FAILED stage=audit");
        return false;
    }
    ++quarantined;
    serial_decision("ZENOV_GUARD_QUARANTINE_OK", result); return true;
}

bool allow_persistent_write(const char* path, const uint8_t* data, uint32_t size, bool append) {
    ++write_scans;
    char normalized[48]{};
    if (!path || !storage::normalize_path(path, normalized) || !enforcement_ready || !zmid::ready ||
        !write_scan_buffer || starts_with_quarantine(normalized)) {
        ++write_blocked;
        serial::write("ZENOV_GUARD_WRITE_BLOCKED path="); serial::write(path ? path : "<null>");
        serial::line(" reason=security-not-ready-or-invalid-path");
        return false;
    }
    const uint8_t* candidate = data;
    uint32_t candidate_size = size;
    if (append) {
        uint32_t existing_size = 0U;
        const int32_t index = storage::find_entry(normalized);
        if (index >= 0) {
            if (storage::zfs_entries[index].type != storage::zfs_type_file ||
                !storage::read_file(normalized, write_scan_buffer, process::application_buffer_bytes, existing_size)) {
                ++write_blocked;
                serial::write("ZENOV_GUARD_WRITE_BLOCKED path="); serial::write(normalized);
                serial::line(" reason=append-read-failed");
                return false;
            }
        }
        if (size > process::application_buffer_bytes - existing_size) {
            ++write_blocked;
            serial::write("ZENOV_GUARD_WRITE_BLOCKED path="); serial::write(normalized);
            serial::line(" reason=scan-limit");
            return false;
        }
        if (size && !data) {
            ++write_blocked;
            serial::write("ZENOV_GUARD_WRITE_BLOCKED path="); serial::write(normalized);
            serial::line(" reason=null-data");
            return false;
        }
        if (size) memcpy(write_scan_buffer + existing_size, data, size);
        candidate = write_scan_buffer;
        candidate_size = existing_size + size;
    }
    ScanResult result = scan_memory(normalized, candidate, candidate_size, false);
    if (result.verdict == Verdict::infected) {
        ++write_blocked;
        if (!record(AuditAction::write_block, result)) return false;
        serial_decision("ZENOV_GUARD_WRITE_BLOCKED", result);
        return false;
    }
    if (result.verdict == Verdict::suspicious) {
        ++audit_only_findings;
        if (!record(AuditAction::scan, result)) return false;
        serial_decision("ZENOV_GUARD_WRITE_AUDIT", result);
    }
    return true;
}

bool allow_persistent_transfer(const char* source, const char* destination) {
    if (!source || !destination || !write_scan_buffer) return false;
    uint32_t size = 0U;
    if (!storage::read_file(source, write_scan_buffer, process::application_buffer_bytes, size)) {
        ++write_blocked;
        serial::write("ZENOV_GUARD_TRANSFER_BLOCKED source="); serial::write(source);
        serial::write(" destination="); serial::write(destination); serial::line(" reason=source-read-failed");
        return false;
    }
    if (!allow_persistent_write(destination, write_scan_buffer, size, false)) {
        serial::write("ZENOV_GUARD_TRANSFER_BLOCKED source="); serial::write(source);
        serial::write(" destination="); serial::line(destination);
        return false;
    }
    return true;
}

bool allow_execution(const char* path, const uint8_t* data, uint32_t size) {
    ScanResult result = scan_memory(path, data, size, true);
    if (!record(AuditAction::execute, result)) result.verdict = Verdict::untrusted;
    if (!enforcement_ready || starts_with_quarantine(result.path)) result.verdict = Verdict::untrusted;
    if (result.verdict == Verdict::trusted) {
        ++trusted_launches; serial_decision("ZENOV_GUARD_EXEC_ALLOWED", result); return true;
    }
    ++blocked;
    if (result.verdict == Verdict::infected) {
        serial_decision("ZENOV_GUARD_DETECTED", result);
        (void)quarantine_path(path);
    } else if (result.verdict == Verdict::untrusted) serial_decision("ZENOV_GUARD_UNTRUSTED_BLOCKED", result);
    else serial_decision("ZENOV_GUARD_POLICY_BLOCKED", result);
    return false;
}

bool sha256_self_test() {
    static constexpr uint8_t expected[sha256_bytes] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    static constexpr uint8_t abc[] = {'a','b','c'};
    uint8_t digest[sha256_bytes]; sha256(abc, sizeof(abc), digest); return digest_equal(digest, expected);
}
bool self_test() {
    static constexpr char test_vector[] = "ZENOV_GUARD_SAFE_TEST_VECTOR_V1";
    static constexpr char clean_vector[] = "ZENOV_GUARD_CLEAN_VECTOR_V1";
    if (!sha256_self_test() || !zmid::ready || !zmid::self_test()) return false;
    const ScanResult detected = scan_memory("<selftest-threat>", reinterpret_cast<const uint8_t*>(test_vector), sizeof(test_vector) - 1U, false);
    const ScanResult clean = scan_memory("<selftest-clean>", reinterpret_cast<const uint8_t*>(clean_vector), sizeof(clean_vector) - 1U, false);
    return detected.verdict == Verdict::infected && clean.verdict == Verdict::clean && sizeof(trusted_executables) / sizeof(trusted_executables[0]) == 7U;
}
bool verify_trusted_baseline() {
    if (!storage::ready() || !process::application_buffer) return false;
    for (const auto& trusted : trusted_executables) {
        uint32_t size = 0; uint8_t digest[sha256_bytes];
        if (!storage::read_file(trusted.path, process::application_buffer, 64U * 1024U, size)) return false;
        sha256(process::application_buffer, size, digest);
        if (!digest_equal(digest, trusted.digest)) return false;
    }
    return true;
}
bool init() {
    self_test_passed = self_test();
    if (!self_test_passed) { serial::line("ZENOV_GUARD_SELFTEST_FAILED"); return false; }
    serial::line("ZENOV_GUARD_SELFTEST_OK");
    baseline_verified = verify_trusted_baseline();
    if (!baseline_verified) { serial::line("ZENOV_GUARD_TRUST_BASELINE_FAILED"); enforcement_ready = false; return true; }
    serial::line("ZENOV_GUARD_TRUST_BASELINE_OK");
    enforcement_ready = true;
    ScanResult result{}; result.verdict = Verdict::trusted; storage::path_copy(result.path, "<boot-baseline>", sizeof(result.path));
    if (!record(AuditAction::boot, result)) return false;
    serial::line("ZENOV_GUARD_READY"); return true;
}

void print_result(const ScanResult& result) {
    console::write("  Path          "); console::line(result.path);
    console::write("  Verdict       "); console::line(verdict_name(result.verdict));
    console::write("  Size          "); console::unsigned_dec(result.size); console::line(" bytes");
    console::write("  Executable    "); console::line(result.executable ? "yes" : "no");
    if (result.signature[0]) { console::write("  Signature     "); console::line(result.signature); }
    console::write("  SHA-256       ");
    static constexpr char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < sha256_bytes; ++i) { console::put(hex[result.digest[i] >> 4U]); console::put(hex[result.digest[i] & 0x0FU]); }
    console::put('\n');
}
void print_status() {
    console::write("  Engine        ZenovGuard 0.1.1\n");
    console::write("  Enforcement   "); console::line(enforcement_ready ? "fail-closed / trusted executables only" : "locked / security state unavailable");
    console::write("  SHA-256       "); console::line(self_test_passed ? "self-test passed" : "failed");
    console::write("  Trust base    "); console::line(baseline_verified ? "7 bundled applications verified" : "not verified");
    console::write("  Intelligence  "); console::unsigned_dec(zmid::active_record_count); console::line(" signed rules");
    console::write("  Scans         "); console::unsigned_dec(scans); console::put('\n');
    console::write("  Blocked       "); console::unsigned_dec(blocked); console::put('\n');
    console::write("  Quarantined   "); console::unsigned_dec(quarantined); console::put('\n');
    console::write("  Trusted runs  "); console::unsigned_dec(trusted_launches); console::put('\n');
    console::write("  Write scans   "); console::unsigned_dec(write_scans); console::put('\n');
    console::write("  Write blocks  "); console::unsigned_dec(write_blocked); console::put('\n');
    console::write("  Audit-only    "); console::unsigned_dec(audit_only_findings); console::put('\n');
    serial::line("ZENOV_GUARD_STATUS_OK");
}
void print_audit() {
    if (!audit_count) { console::line("  <no volatile security events>"); return; }
    const uint32_t first = (audit_next + audit_capacity - audit_count) % audit_capacity;
    for (uint32_t i = 0; i < audit_count; ++i) {
        const AuditRecord& entry = audit_log[(first + i) % audit_capacity];
        console::write("  "); console::unsigned_dec(entry.tick); console::write("  "); console::write(action_name(entry.action));
        console::write("  "); console::write(verdict_name(entry.verdict)); console::write("  "); console::line(entry.path);
    }
}
void print_quarantine() {
    uint32_t files = 0U;
    for (uint32_t i = 0U; i < storage::zfs_super.entry_count; ++i) {
        const auto& entry = storage::zfs_entries[i];
        if (!entry.used || entry.type != storage::zfs_type_file || !starts_with_quarantine(entry.path)) continue;
        console::write("  "); console::write(entry.path); console::write("  "); console::unsigned_dec(entry.size); console::line(" bytes");
        ++files;
    }
    if (!files) console::line("  <quarantine empty>");
    console::write("  Entries       "); console::unsigned_dec(files); console::put('\n');
    serial::write("ZENOV_GUARD_QUARANTINE_LIST_OK entries="); process::serial_u32(files); serial::write("\r\n");
}

bool delete_quarantine(const char* path) {
    char normalized[48]{};
    if (!path || !storage::normalize_path(path, normalized) || !starts_with_quarantine(normalized)) return false;
    const uint32_t length = static_cast<uint32_t>(string_length(normalized));
    const bool metadata = length > 5U && storage::bytes_equal(normalized + length - 5U, ".meta", 5U);
    char sidecar[48]{};
    if (!metadata) {
        storage::path_copy(sidecar, normalized, sizeof(sidecar));
        if (length + 5U < sizeof(sidecar)) {
            sidecar[length] = '.'; sidecar[length + 1U] = 'm'; sidecar[length + 2U] = 'e';
            sidecar[length + 3U] = 't'; sidecar[length + 4U] = 'a'; sidecar[length + 5U] = 0;
        }
    }
    if (!storage::quarantine_remove(normalized)) return false;
    if (sidecar[0] && storage::find_entry(sidecar) >= 0 && !storage::quarantine_remove(sidecar)) return false;
    ScanResult event{}; event.verdict = Verdict::clean; storage::path_copy(event.path, normalized, sizeof(event.path));
    storage::path_copy(event.signature, "Quarantine.Delete", sizeof(event.signature));
    if (!record(AuditAction::quarantine, event)) return false;
    serial::write("ZENOV_GUARD_QUARANTINE_DELETE_OK path="); serial::line(normalized);
    return true;
}

void scan_all() {
    uint32_t files = 0, clean = 0, findings = 0;
    for (uint32_t i = 0; i < storage::zfs_super.entry_count; ++i) {
        const auto& entry = storage::zfs_entries[i];
        if (!entry.used || entry.type != storage::zfs_type_file || security_policy::protected_path(entry.path)) continue;
        ScanResult result{}; ++files;
        if (!scan_path(entry.path, result)) { ++findings; console::write("  ERROR       "); console::line(entry.path); continue; }
        console::write("  "); console::write(verdict_name(result.verdict));
        for (uint32_t pad = static_cast<uint32_t>(string_length(verdict_name(result.verdict))); pad < 12U; ++pad) console::put(' ');
        console::line(result.path);
        if (result.verdict == Verdict::clean || result.verdict == Verdict::trusted) ++clean; else ++findings;
    }
    console::write("  Files         "); console::unsigned_dec(files); console::put('\n');
    console::write("  Clean/trusted "); console::unsigned_dec(clean); console::put('\n');
    console::write("  Findings      "); console::unsigned_dec(findings); console::put('\n');
    serial::line("ZENOV_GUARD_FULL_SCAN_OK");
}
} // namespace security_guard
