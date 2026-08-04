bool cache_lookup(const uint8_t digest[security_guard::sha256_bytes],
                  const char* path,
                  security_guard::ScanResult& result,
                  bool& matched) {
    for (const auto& entry : cache) {
        if (!entry.valid || entry.database_version != active_database_version ||
            !path_context_equal(entry.path_context, path) ||
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
                 const char* path,
                 bool matched,
                 const security_guard::ScanResult& result) {
    CacheEntry& entry = cache[cache_next++ % cache_capacity];
    entry = CacheEntry{};
    entry.valid = true;
    entry.matched = matched;
    entry.database_version = active_database_version;
    storage::path_copy(entry.path_context, path ? path : "", sizeof(entry.path_context));
    memcpy(entry.digest, digest, sizeof(entry.digest));
    if (matched) {
        entry.verdict = result.verdict;
        entry.action = result.policy_action;
        storage::path_copy(entry.signature, result.signature, sizeof(entry.signature));
    }
}

void serial_u32_value(uint32_t value) {
    char digits[10]{};
    uint32_t count = 0U;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count) serial::put(digits[--count]);
}

void print_workspace_failure() {
    serial::write("ZMID_CONTENT_WORKSPACE_FAILED reason=");
    serial::write(heap::failure_name());
    serial::write(" index=");
    serial_u32_value(heap::failure_index);
    serial::write(" actual=");
    serial_u32_value(heap::failure_actual);
    serial::write(" expected=");
    serial_u32_value(heap::failure_expected);
    serial::line("");
}

bool self_test() {
    if (!ensure_workspace()) {
        print_workspace_failure();
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=workspace-map");
        return false;
    }
    WorkspaceMappingGuard mapping_guard(true);
    workspace_a[0] = 0xA5U;
    workspace_a[workspace_bytes - 1U] = 0x5AU;
    workspace_b[0] = 0x3CU;
    workspace_b[workspace_bytes - 1U] = 0xC3U;
    if (workspace_a[0] != 0xA5U || workspace_a[workspace_bytes - 1U] != 0x5AU ||
        workspace_b[0] != 0x3CU || workspace_b[workspace_bytes - 1U] != 0xC3U) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=workspace-alias");
        return false;
    }
    memset(workspace_a, 0, workspace_bytes);
    memset(workspace_b, 0, workspace_bytes);

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
        result.verdict != security_guard::Verdict::infected) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=casefold");
        return false;
    }
    result = {};
    if (inspect_payload("<content-base64-selftest>", base64_vector, sizeof(base64_vector) - 1U,
            0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=base64");
        return false;
    }
    result = {};
    if (inspect_payload("<content-base64-token-selftest>", embedded_base64_vector,
            sizeof(embedded_base64_vector) - 1U, 0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=base64-token");
        return false;
    }
    result = {};
    if (inspect_payload("<content-base64-unpadded-selftest>", unpadded_base64_vector,
            sizeof(unpadded_base64_vector) - 1U, 0U, workspace_a, workspace_b, result) != InspectionResult::matched ||
        result.verdict != security_guard::Verdict::infected) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=base64-unpadded");
        return false;
    }

    uint32_t expanded = 0U;
    if (!inflate_raw(deflate_vector, sizeof(deflate_vector), workspace_a,
            workspace_bytes, expanded)) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=deflate-decode");
        return false;
    }
    if (expanded != sizeof(expected) - 1U) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=deflate-size");
        return false;
    }
    if (!storage::bytes_equal(workspace_a, expected, expanded)) {
        serial::line("ZMID_CONTENT_SELFTEST_FAILED stage=deflate-bytes");
        return false;
    }
    clear_cache();
    serial::line("ZMID_CONTENT_SELFTEST_OK workspace=distinct stages=5");
    return true;
}

bool init() {
    if (!self_test()) return false;
    serial::line("ZMID_CONTENT_INSPECTION_OK base64=2 tokens=1 casefold=1 zip=2 eocd=1 sfx=1 sfx-prefix=1 alias-safe=1 deflate=1 depth=2 entries=16 expanded=65536 size-limit=closed cache=16 cache-path=1");
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

    storage::path_copy(result.path, path ? path : "", sizeof(result.path));
    if (size > content_inspection::workspace_bytes) {
        result.verdict = security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, "Inspection.SizeLimit", sizeof(result.signature));
        return true;
    }

    bool matched = false;
    if (content_inspection::cache_lookup(digest, path, result, matched)) return matched;
    if (!content_inspection::ensure_workspace()) {
        result.verdict = security_guard::Verdict::suspicious;
        storage::path_copy(result.signature, "Inspection.Workspace", sizeof(result.signature));
        return true;
    }
    content_inspection::WorkspaceMappingGuard mapping_guard(true);

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
    content_inspection::cache_store(digest, path, matched, result);
    return matched;
}

} // namespace zmid
