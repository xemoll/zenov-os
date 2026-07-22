#include <cstdint>
#include <cstdio>
#include <cstring>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

namespace storage {
#include "../kernel/parts/storage_ata_recovery_policy.inc"
}

namespace {
int failures = 0;
int cases = 0;
void expect(bool condition, const char* label) {
    ++cases;
    if (!condition) {
        std::fprintf(stderr, "ATA_RECOVERY_POLICY_TEST_FAIL case=%s\n", label);
        ++failures;
    }
}
}

int main() {
    using storage::AtaRevalidationDecision;
    using storage::ata_revalidation_decision;
    using storage::ata_revalidation_reason;

    expect(ata_revalidation_decision(true, 0U, 32768U) == AtaRevalidationDecision::accept,
           "initial-identify");
    expect(ata_revalidation_decision(true, 32768U, 32768U) == AtaRevalidationDecision::accept,
           "stable-capacity");
    expect(ata_revalidation_decision(false, 32768U, 32768U) == AtaRevalidationDecision::identify_failed,
           "identify-failed");
    expect(ata_revalidation_decision(true, 32768U, 0U) == AtaRevalidationDecision::missing_capacity,
           "missing-after-reset");
    expect(ata_revalidation_decision(true, 0U, 0U) == AtaRevalidationDecision::missing_capacity,
           "missing-initial-device");
    expect(ata_revalidation_decision(true, 32768U, 32769U) == AtaRevalidationDecision::capacity_changed,
           "changed-capacity");
    expect(std::strcmp(ata_revalidation_reason(AtaRevalidationDecision::identify_failed), "identify") == 0,
           "identify-reason");
    expect(std::strcmp(ata_revalidation_reason(AtaRevalidationDecision::missing_capacity), "missing-capacity") == 0,
           "missing-reason");
    expect(std::strcmp(ata_revalidation_reason(AtaRevalidationDecision::capacity_changed), "capacity-changed") == 0,
           "changed-reason");

    if (failures != 0) return 1;
    std::printf("ATA_RECOVERY_POLICY_TEST_OK cases=%d\n", cases);
    return 0;
}
