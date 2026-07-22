enum class AtaRevalidationDecision : uint8_t {
    accept,
    identify_failed,
    missing_capacity,
    capacity_changed
};

constexpr AtaRevalidationDecision ata_revalidation_decision(bool identify_ok,
                                                             uint32_t expected_sectors,
                                                             uint32_t observed_sectors) {
    if (!identify_ok) return AtaRevalidationDecision::identify_failed;
    if (observed_sectors == 0U) return AtaRevalidationDecision::missing_capacity;
    if (expected_sectors != 0U && observed_sectors != expected_sectors) {
        return AtaRevalidationDecision::capacity_changed;
    }
    return AtaRevalidationDecision::accept;
}

constexpr const char* ata_revalidation_reason(AtaRevalidationDecision decision) {
    switch (decision) {
        case AtaRevalidationDecision::accept: return "accepted";
        case AtaRevalidationDecision::identify_failed: return "identify";
        case AtaRevalidationDecision::missing_capacity: return "missing-capacity";
        case AtaRevalidationDecision::capacity_changed: return "capacity-changed";
    }
    return "unknown";
}
