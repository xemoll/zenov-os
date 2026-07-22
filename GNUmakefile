# GNU make loads this before Makefile. Preserve the upstream 0.1.1 build graph,
# then layer ZenPkg, the signed offline ZenRepo trust chain and ZenUniverse.
include Makefile
include zenpkg.mk
include universe.mk

# zenpkg-check validates the generated manifest in its recipe. Extending the
# target here makes that dependency explicit without duplicating zenpkg.mk.
$(ZENPKG_CHECK_STAMP): $(BUILD)/zenpkg-manifest.json

ATA_POLICY_TEST := $(BUILD)/storage-ata-policy-test
ATA_RECOVERY_POLICY_TEST := $(BUILD)/storage-ata-recovery-policy-test
BLOCK_RESULT_TEST := $(BUILD)/storage-block-result-test
ATA_EIO_QEMU_OUT := $(BUILD)/qemu/ata-eio-retry
ATA_EIO_QEMU_STAMP := $(ATA_EIO_QEMU_OUT)/.stamp
BLOCK_STATUS_QEMU_OUT := $(BUILD)/qemu/block-status
BLOCK_STATUS_QEMU_STAMP := $(BLOCK_STATUS_QEMU_OUT)/.stamp

.PHONY: ata-eio-qemu block-status-qemu

$(ATA_POLICY_TEST): tests/storage_ata_policy_test.cpp kernel/parts/storage_ata_policy.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(ATA_RECOVERY_POLICY_TEST): tests/storage_ata_recovery_policy_test.cpp kernel/parts/storage_ata_recovery_policy.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(BLOCK_RESULT_TEST): tests/storage_block_result_test.cpp kernel/parts/storage_block_result.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

check: $(ATA_POLICY_TEST) $(ATA_RECOVERY_POLICY_TEST) $(BLOCK_RESULT_TEST)

$(ATA_EIO_QEMU_STAMP): all tests/qemu_ata_eio_retry.sh tests/blkdebug/ata-write-eio-once.conf $(BUILD)/zenovfs-verify
	@rm -rf $(ATA_EIO_QEMU_OUT)
	@mkdir -p $(ATA_EIO_QEMU_OUT)
	bash tests/qemu_ata_eio_retry.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  tests/blkdebug/ata-write-eio-once.conf \
	  $(ATA_EIO_QEMU_OUT)
	$(BUILD)/zenovfs-verify $(ATA_EIO_QEMU_OUT)/runtime.img
	@touch $@

ata-eio-qemu: $(ATA_EIO_QEMU_STAMP)

$(BLOCK_STATUS_QEMU_STAMP): all tests/qemu_block_status.sh $(BUILD)/zenovfs-verify
	@rm -rf $(BLOCK_STATUS_QEMU_OUT)
	@mkdir -p $(BLOCK_STATUS_QEMU_OUT)
	bash tests/qemu_block_status.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  $(BLOCK_STATUS_QEMU_OUT)
	$(BUILD)/zenovfs-verify $(BLOCK_STATUS_QEMU_OUT)/runtime.img
	@touch $@

block-status-qemu: $(BLOCK_STATUS_QEMU_STAMP)
