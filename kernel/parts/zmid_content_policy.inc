bool cache_lookup(const uint8_t digest[security_guard::sha256_bytes],
                  security_guard::ScanResult& result,
                  bool& matched) {
    for (const auto& entry : cache) {
        if (!entry.valid || entry.database_version != active_database_version ||
            !security_guard::digest_equal(entry.digest, digest)) continue;
        ++cache_hits;
        matched = entry.matched;
        if (matched) {
            result.verdict = entry.verdict;
            result.policy_action = entry.action;
            storage::path_copy(result.signature, entry.signature, sizeof(result.signature));
        }
        return true;
    }
    return false;
}

void cache_store(const uint8_t digest[security_guard::sha256_bytes],
                 bool matched,
                 const security_guard::ScanResult& result) {
    CacheEntry& entry = cache[cache_next++ % cache_capacity];
    entry = CacheEntry{};
    entry.valid = true;
    entry.matched = matched;
    entry.database_version = active_database_version;
    memcpy(entry.digest, digest, sizeof(entry.digest));
    if (matched) {
        entry.verdict = result.verdict;
        entry.action = result.policy_action;
        storage::path_copy(entry.signature, result.signature, sizeof(entry.signature));
    }
}

bool self_test() {
    if (!ensure_workspace()) return false;
    static constexpr uint8_t case_vector[] = "zenov_ransomware_test_v1";
    static constexpr uint8_t base64_vector[] = "WkVOT1ZfUkFOU09NV0FSRV9URVNUX1Yx";
    static constexpr uint8_t embedded_base64_vector[] = "payload=WkVOT1ZfUkFOU09NV0FSRV9URVNUX1Yx";
    static constexpr uint8_t unpadded_base64_vector[] = "WkVOT1ZfUkFOU09NV0FSRV9URVNUX1YxIQ";
    static constexpr uint8_t deflate_vector[] = {
        0x8B,0x72,0xF5,0xF3,0x0F,0x8B,0x0F,0x72,0xF4,0x0B,0xF6,0xF7,0x0D,
        0x77,0x0C,0x72,0x8D,0x0F,0x71,0x0D,0x0E,0x89,0x0F,0x33,0x04,0x00
    };
    static constexpr uint8_t expected[] = "ZENOV_RANSOMWARE_TEST_V1";

    security_guard::ScanResult result{};
    if (inspect_payload("<content-case-selftest>", case_vector, sizeof(case_vector) - 1U,
            0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) return false;
    result = {};
    if (inspect_payload("<content-base64-selftest>", base64_vector, sizeof(base64_vector) - 1U,
            0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) return false;
    result = {};
    if (inspect_payload("<content-base64-token-selftest>", embedded_base64_vector,
            sizeof(embedded_base64_vector) - 1U, 0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) return false;
    result = {};
    if (inspect_payload("<content-base64-unpadded-selftest>", unpadded_base64_vector,
            sizeof(unpadded_base64_vector) - 1U, 0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) return false;

    uint32_t expanded = 0U;
    if (!inflate_raw(deflate_vector, sizeof(deflate_vector), workspace_a,
            workspace_bytes, expanded) || expanded != sizeof(expected) - 1U ||
        !storage::bytes_equal(workspace_a, expected, expanded)) return false;
    clear_cache();
    return true;
}

bool init() {
    if (!self_test()) return false;
    serial::line("ZMID_CONTENT_INSPECTION_OK base64=2 tokens=1 casefold=1 zip=2 eocd=1 sfx=1 deflate=1 depth=2 entries=16 expanded=65536 cache=16");
    return true;
}

} // namespace content_inspection

bool classify(const char* path,
              const uint8_t* data,
              uint32_t size,
              const uint8_t digest[security_guard::sha256_bytes],
              security_guard::ScanResult& result) {
    if (classify_raw(path, data, size, digest, result)) {
        ++content_inspection::raw_hits;
        return true;
    }
    if (!ready || (!data && size) || !digest) return false;

    bool matched = false;
    if (content_inspection::cache_lookup(digest, result, matched)) return matched;
    if (!content_inspection::ensure_workspace()) {
        result.verdict = security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, "Inspection.Workspace", sizeof(result.signature));
        return true;
    }

    const content_inspection::InspectionResult inspection = content_inspection::inspect_payload(
        path, data, size, 0U, content_inspection::workspace_a,
        content_inspection::workspace_b, result);
    if (inspection == content_inspection::InspectionResult::unsupported) {
        ++content_inspection::unsupported_containers;
        result.verdict = security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, "Container.Unsupported", sizeof(result.signature));
        matched = true;
    } else {
        matched = inspection == content_inspection::InspectionResult::matched;
        if (matched) ++content_inspection::transformed_hits;
    }
    content_inspection::cache_store(digest, matched, result);
    return matched;
}

} // namespace zmid
