# GNU make loads this before Makefile. Preserve the upstream 0.1.1 build graph,
# then layer ZenPkg, the signed offline ZenRepo trust chain and ZenUniverse.
include Makefile
include zenpkg.mk
include universe.mk

# zenpkg-check validates the generated manifest in its recipe. Extending the
# target here makes that dependency explicit without duplicating zenpkg.mk.
$(ZENPKG_CHECK_STAMP): $(BUILD)/zenpkg-manifest.json

ATA_POLICY_TEST := $(BUILD)/storage-ata-policy-test

$(ATA_POLICY_TEST): tests/storage_ata_policy_test.cpp kernel/parts/storage_ata_policy.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

check: $(ATA_POLICY_TEST)
