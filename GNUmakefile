# GNU make loads this before Makefile. Preserve the upstream 0.1.1 build graph,
# then layer ZenPkg, the signed offline ZenRepo trust chain and ZenUniverse.
include Makefile
include zenpkg.mk
include universe.mk

# zenpkg-check validates the generated manifest in its recipe. Extending the
# target here makes that dependency explicit without duplicating zenpkg.mk.
ZENPKG_FOREIGN_FORMAT_TEST := $(BUILD)/package-foreign-format-test
ZENPKG_FOREIGN_CHECK_OUT := $(BUILD)/zenpkg-foreign-test
ZENPKG_FOREIGN_CHECK_STAMP := $(ZENPKG_FOREIGN_CHECK_OUT)/.stamp
ZENPKG_FOREIGN_QEMU_OUT := $(BUILD)/qemu/zenpkg-foreign
ZENPKG_FOREIGN_QEMU_STAMP := $(ZENPKG_FOREIGN_QEMU_OUT)/.stamp
ZENPKG_FOREIGN_FORMAT_SRC := kernel/parts/package_foreign_format.inc kernel/parts/package_foreign_policy.inc
ZENPKG_FOREIGN_HOST_SRC := tools/zenpkg/foreign.hpp tools/zenpkg/foreign_import.hpp tools/zenpkg/foreign_probe.hpp

$(ZENPKG_CHECK_STAMP): $(BUILD)/zenpkg-manifest.json $(ZENPKG_FOREIGN_CHECK_STAMP)
$(BUILD)/zenpkg: $(ZENPKG_FOREIGN_FORMAT_SRC) $(ZENPKG_FOREIGN_HOST_SRC)
$(BUILD)/kernel.o: $(ZENPKG_FOREIGN_FORMAT_SRC) kernel/parts/package_manager/formats.inc
$(BUILD)/build-manifest.json: $(ZENPKG_FOREIGN_FORMAT_SRC) kernel/parts/package_manager/formats.inc

ATA_POLICY_TEST := $(BUILD)/storage-ata-policy-test
ATA_RECOVERY_POLICY_TEST := $(BUILD)/storage-ata-recovery-policy-test
BLOCK_RESULT_TEST := $(BUILD)/storage-block-result-test
BLOCK_DEVICE_ABI_TEST := $(BUILD)/storage-block-device-abi-test
ATA_EIO_QEMU_OUT := $(BUILD)/qemu/ata-eio-retry
ATA_EIO_QEMU_STAMP := $(ATA_EIO_QEMU_OUT)/.stamp
BLOCK_STATUS_QEMU_OUT := $(BUILD)/qemu/block-status
BLOCK_STATUS_QEMU_STAMP := $(BLOCK_STATUS_QEMU_OUT)/.stamp

.PHONY: ata-eio-qemu block-status-qemu zenpkg-foreign-check zenpkg-foreign-qemu

$(ZENPKG_FOREIGN_FORMAT_TEST): tests/package_foreign_format_test.cpp $(ZENPKG_FOREIGN_FORMAT_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(ZENPKG_FOREIGN_CHECK_STAMP): $(ZENPKG_FOREIGN_FORMAT_TEST) $(BUILD)/zenpkg $(BUILD)/HELLO.ZEX tests/zenpkg_foreign_test.sh tests/zenpkg_streaming_test.sh $(ZENPKG_FOREIGN_HOST_SRC) $(ZENPKG_FOREIGN_FORMAT_SRC)
	@rm -rf $(ZENPKG_FOREIGN_CHECK_OUT)
	@mkdir -p $(ZENPKG_FOREIGN_CHECK_OUT)
	$(ZENPKG_FOREIGN_FORMAT_TEST)
	bash tests/zenpkg_foreign_test.sh $(BUILD)/zenpkg $(BUILD)/HELLO.ZEX $(ZENPKG_FOREIGN_CHECK_OUT)
	bash tests/zenpkg_streaming_test.sh $(BUILD)/zenpkg $(ZENPKG_FOREIGN_CHECK_OUT)/streaming
	@touch $@

zenpkg-foreign-check: $(ZENPKG_FOREIGN_CHECK_STAMP)

$(ZENPKG_FOREIGN_QEMU_STAMP): all tests/qemu_zenpkg_foreign.sh
	@rm -rf $(ZENPKG_FOREIGN_QEMU_OUT)
	@mkdir -p $(ZENPKG_FOREIGN_QEMU_OUT)
	bash tests/qemu_zenpkg_foreign.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  $(ZENPKG_FOREIGN_QEMU_OUT)
	$(BUILD)/zenovfs-verify $(ZENPKG_FOREIGN_QEMU_OUT)/runtime.img
	@touch $@

zenpkg-foreign-qemu: $(ZENPKG_FOREIGN_QEMU_STAMP)
zenpkg-qemu: zenpkg-foreign-qemu
qemu: zenpkg-foreign-qemu

$(ATA_POLICY_TEST): tests/storage_ata_policy_test.cpp kernel/parts/storage_ata_policy.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(ATA_RECOVERY_POLICY_TEST): tests/storage_ata_recovery_policy_test.cpp kernel/parts/storage_ata_recovery_policy.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(BLOCK_RESULT_TEST): tests/storage_block_result_test.cpp kernel/parts/storage_block_result.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(BLOCK_DEVICE_ABI_TEST): tests/storage_block_device_test.cpp kernel/parts/storage_block_result.inc kernel/parts/storage_block_device.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

check: $(ATA_POLICY_TEST) $(ATA_RECOVERY_POLICY_TEST) $(BLOCK_RESULT_TEST) $(BLOCK_DEVICE_ABI_TEST)

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
