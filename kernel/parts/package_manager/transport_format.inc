constexpr const char* transport_state_path = "/var/lib/zenpkg/transport.v1";
constexpr uint32_t transport_chunk_bytes = 512U;
constexpr uint32_t transport_retry_limit = 3U;
constexpr uint32_t transport_provider_offline = 1U;
constexpr uint32_t transport_phase_downloading = 1U;
constexpr uint32_t transport_phase_ready = 2U;
constexpr uint32_t transport_phase_failed = 3U;

struct [[gnu::packed]] TransportJournal {
    char magic[8];
    uint32_t schema;
    uint32_t generation;
    uint32_t phase;
    uint32_t offset;
    uint32_t expected_length;
    uint32_t provider;
    uint32_t retries;
    uint32_t checksum;
    char name[32];
    char version[16];
    char source_path[48];
    char partial_path[48];
    uint8_t package_digest[32];
    uint8_t prefix_digest[32];
};

static_assert(sizeof(TransportJournal) == 248U);

uint32_t transport_journal_checksum(const TransportJournal& input) {
    TransportJournal copy = input;
    copy.checksum = 0;
    return storage::fnv1a(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool transport_magic_valid(const TransportJournal& input) {
    const char expected[8] = {'Z','P','T','R','N','1',0,0};
    return storage::bytes_equal(input.magic, expected, sizeof(expected));
}

bool transport_journal_valid(const TransportJournal& input) {
    if (!transport_magic_valid(input) || input.schema != 1U || input.generation == 0U ||
        input.provider != transport_provider_offline || input.expected_length == 0U ||
        input.expected_length > maximum_package_bytes || input.offset > input.expected_length ||
        input.checksum != transport_journal_checksum(input)) return false;
    if (input.phase != transport_phase_downloading && input.phase != transport_phase_ready &&
        input.phase != transport_phase_failed) return false;
    if (input.phase == transport_phase_ready && input.offset != input.expected_length) return false;
    if (!input.name[0] || !input.version[0] || !input.source_path[0] || !input.partial_path[0] ||
        input.name[sizeof(input.name) - 1U] || input.version[sizeof(input.version) - 1U] ||
        input.source_path[sizeof(input.source_path) - 1U] || input.partial_path[sizeof(input.partial_path) - 1U]) return false;
    return true;
}
