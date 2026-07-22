#include <cstdint>
#include <cstdio>

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
    using storage::ata_capacity_revalidated;
    expect(ata_capacity_revalidated(0U, 32768U), "initial-identify");
    expect(ata_capacity_revalidated(32768U, 32768U), "stable-capacity");
    expect(!ata_capacity_revalidated(32768U, 32769U), "changed-capacity");
    expect(!ata_capacity_revalidated(32768U, 0U), "missing-after-reset");
    expect(!ata_capacity_revalidated(0U, 0U), "missing-initial-device");
    if (failures != 0) return 1;
    std::printf("ATA_RECOVERY_POLICY_TEST_OK cases=%d\n", cases);
    return 0;
}
