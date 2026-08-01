namespace zmid {
namespace content_inspection {

constexpr uint32_t workspace_bytes = 64U * 1024U;
constexpr uint32_t cache_capacity = 16U;
constexpr uint32_t max_archive_depth = 2U;
constexpr uint32_t max_archive_entries = 16U;
constexpr uint32_t max_expanded_total = 64U * 1024U;
constexpr uint32_t max_expansion_ratio = 128U;
constexpr uint32_t max_deflate_blocks = 128U;

uint8_t* workspace_a = nullptr;
uint8_t* workspace_b = nullptr;
uint32_t cache_next = 0U;
uint32_t raw_hits = 0U;
uint32_t transformed_hits = 0U;
uint32_t cache_hits = 0U;
uint32_t unsupported_containers = 0U;

struct CacheEntry {
    bool valid = false;
    bool matched = false;
    uint32_t database_version = 0U;
    uint8_t digest[security_guard::sha256_bytes]{};
    security_guard::Verdict verdict = security_guard::Verdict::error;
    uint8_t action = 0U;
    char signature[48]{};
};

CacheEntry cache[cache_capacity]{};

uint16_t load_le16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t load_le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8U) |
        (static_cast<uint32_t>(data[2]) << 16U) |
        (static_cast<uint32_t>(data[3]) << 24U);
}

bool range_fits(uint32_t offset, uint32_t length, uint32_t size) {
    return offset <= size && length <= size - offset;
}

bool expansion_within_limit(uint32_t compressed_size, uint32_t expanded_size) {
    return static_cast<unsigned long long>(expanded_size) <=
        static_cast<unsigned long long>(compressed_size) * max_expansion_ratio + 1024ULL;
}

bool ensure_workspace() {
    if (workspace_a && workspace_b) return true;
    workspace_a = static_cast<uint8_t*>(heap::allocate(workspace_bytes, 16U));
    workspace_b = static_cast<uint8_t*>(heap::allocate(workspace_bytes, 16U));
    return workspace_a && workspace_b;
}

void clear_cache() {
    for (auto& entry : cache) entry = CacheEntry{};
    cache_next = 0U;
}

void compose_signature(char output[48], const char* layer, const char* signature) {
    uint32_t used = 0U;
    auto append = [&](const char* text) {
        while (text && *text && used + 1U < 48U) output[used++] = *text++;
    };
    append(layer);
    append(":");
    append(signature && *signature ? signature : "Matched");
    output[used] = 0;
}

void copy_match(security_guard::ScanResult& output,
                const security_guard::ScanResult& inner,
                const char* layer) {
    output.verdict = inner.verdict;
    output.policy_action = inner.policy_action;
    compose_signature(output.signature, layer, inner.signature);
}

bool classify_decoded(const char* path,
                      const uint8_t* data,
                      uint32_t size,
                      security_guard::ScanResult& output,
                      const char* layer) {
    uint8_t digest[security_guard::sha256_bytes]{};
    security_guard::sha256(data, size, digest);
    security_guard::ScanResult inner{};
    if (!classify_raw(path, data, size, digest, inner)) return false;
    copy_match(output, inner, layer);
    return true;
}
