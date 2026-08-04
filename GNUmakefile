# GNU make loads this before Makefile. Preserve the upstream 0.1.1 build graph,
# then layer ZenPkg, the signed offline ZenRepo trust chain and ZenUniverse.
include Makefile
include zenpkg.mk
include universe.mk

# The freestanding kernel is one translation unit and must remain entirely below
# the VGA aperture. Optimize that image for size while keeping host tooling at O2.
KERNEL_FLAGS := $(filter-out -O2,$(KERNEL_FLAGS)) -Os

# zenpkg-check validates the generated manifest in its recipe. Extending the
# target here makes that dependency explicit without duplicating zenpkg.mk.
ZENPKG_FOREIGN_FORMAT_TEST := $(BUILD)/package-foreign-format-test
ZENPKG_NATIVE_SNAPSHOT_TEST := $(BUILD)/zenpkg-native-snapshot-test
ZENPKG_SHA256_TEST := $(BUILD)/zenpkg-sha256-test
LINUX_I386_ABI_TEST := $(BUILD)/linux-i386-abi-test
LINUX_I386_ELF_TEST := $(BUILD)/linux-i386-elf-test
ZENPKG_COMPAT_PREFLIGHT_TEST := $(BUILD)/package-compatibility-preflight-test
ZENPKG_FOREIGN_CHECK_OUT := $(BUILD)/zenpkg-foreign-test
ZENPKG_FOREIGN_CHECK_STAMP := $(ZENPKG_FOREIGN_CHECK_OUT)/.stamp
ZENPKG_FOREIGN_QEMU_OUT := $(BUILD)/qemu/zenpkg-foreign
ZENPKG_FOREIGN_QEMU_STAMP := $(ZENPKG_FOREIGN_QEMU_OUT)/.stamp
ZENPKG_FOREIGN_FORMAT_SRC := kernel/parts/package_foreign_format.inc kernel/parts/package_foreign_policy.inc
ZENPKG_COMPAT_PREFLIGHT_SRC := kernel/parts/package_compatibility_preflight.inc kernel/parts/linux_i386_elf.inc
ZENPKG_FOREIGN_HOST_SRC := tools/zenpkg/foreign.hpp tools/zenpkg/foreign_import.hpp tools/zenpkg/foreign_probe.hpp
ZENPKG_SHA256_SRC := tools/zenpkg/sha256.hpp tools/zenpkg/common.hpp

POLICY_JOURNAL_TEST := $(BUILD)/security-policy-journal-test
POLICY_JOURNAL_FIXTURE := $(BUILD)/zenovfs-policy-journal-fixture
POLICY_JOURNAL_HOT_IMAGE := $(BUILD)/zenov-data-policy-journal-hot.img
POLICY_JOURNAL_CORRUPT_IMAGE := $(BUILD)/zenov-data-policy-journal-corrupt.img
POLICY_JOURNAL_QEMU_OUT := $(BUILD)/qemu/policy-journal
POLICY_JOURNAL_QEMU_STAMP := $(POLICY_JOURNAL_QEMU_OUT)/.stamp
POLICY_JOURNAL_SRC := kernel/parts/security_policy_format.inc kernel/parts/security_policy_transaction.inc

TPM2_PROTOCOL_TEST := $(BUILD)/tpm2-protocol-test
TPM2_QEMU_OUT := $(BUILD)/qemu/tpm2-nv
TPM2_QEMU_STAMP := $(TPM2_QEMU_OUT)/.stamp
TPM2_SRC := kernel/parts/tpm2_protocol.inc kernel/parts/tpm2_tis.inc kernel/parts/tpm2_commands.inc

$(ZENPKG_CHECK_STAMP): $(BUILD)/zenpkg-manifest.json $(ZENPKG_FOREIGN_CHECK_STAMP)
$(BUILD)/zenpkg: $(ZENPKG_FOREIGN_FORMAT_SRC) $(ZENPKG_FOREIGN_HOST_SRC) $(ZENPKG_SHA256_SRC)
$(BUILD)/kernel.o: $(ZENPKG_FOREIGN_FORMAT_SRC) $(ZENPKG_COMPAT_PREFLIGHT_SRC) kernel/parts/package_manager/formats.inc kernel/parts/package_manager/commands_compat.inc $(POLICY_JOURNAL_SRC) $(TPM2_SRC)
$(BUILD)/build-manifest.json: $(ZENPKG_FOREIGN_FORMAT_SRC) kernel/parts/package_manager/formats.inc $(POLICY_JOURNAL_SRC) $(TPM2_SRC)

ATA_POLICY_TEST := $(BUILD)/storage-ata-policy-test
ATA_RECOVERY_POLICY_TEST := $(BUILD)/storage-ata-recovery-policy-test
BLOCK_RESULT_TEST := $(BUILD)/storage-block-result-test
BLOCK_DEVICE_ABI_TEST := $(BUILD)/storage-block-device-abi-test
ZENOVFS_RESULT_TEST := $(BUILD)/storage-fs-result-test
ATA_EIO_QEMU_OUT := $(BUILD)/qemu/ata-eio-retry
ATA_EIO_QEMU_STAMP := $(ATA_EIO_QEMU_OUT)/.stamp
ATA_READ_FAULT_OUT := $(BUILD)/qemu/ata-read-faults
ATA_READ_FAULT_STAMP := $(ATA_READ_FAULT_OUT)/.stamp
BLOCK_STATUS_QEMU_OUT := $(BUILD)/qemu/block-status
BLOCK_STATUS_QEMU_STAMP := $(BLOCK_STATUS_QEMU_OUT)/.stamp

.PHONY: ata-eio-qemu ata-read-fault-qemu block-status-qemu zenpkg-foreign-check zenpkg-foreign-qemu policy-journal-qemu tpm2-qemu

$(ZENPKG_FOREIGN_FORMAT_TEST): tests/package_foreign_format_test.cpp $(ZENPKG_FOREIGN_FORMAT_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(ZENPKG_NATIVE_SNAPSHOT_TEST): tests/zenpkg_native_snapshot_test.cpp $(ZENPKG_FOREIGN_HOST_SRC) $(ZENPKG_FOREIGN_FORMAT_SRC) $(ZENPKG_SHA256_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(ZENPKG_SHA256_TEST): tests/zenpkg_sha256_test.cpp $(ZENPKG_SHA256_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(LINUX_I386_ABI_TEST): tests/linux_i386_abi_test.cpp kernel/parts/linux_i386_abi.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(LINUX_I386_ELF_TEST): tests/linux_i386_elf_test.cpp kernel/parts/linux_i386_elf.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(ZENPKG_COMPAT_PREFLIGHT_TEST): tests/package_compatibility_preflight_test.cpp $(ZENPKG_FOREIGN_FORMAT_SRC) $(ZENPKG_COMPAT_PREFLIGHT_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(ZENPKG_FOREIGN_CHECK_STAMP): $(ZENPKG_FOREIGN_FORMAT_TEST) $(ZENPKG_NATIVE_SNAPSHOT_TEST) $(ZENPKG_SHA256_TEST) $(LINUX_I386_ABI_TEST) $(LINUX_I386_ELF_TEST) $(ZENPKG_COMPAT_PREFLIGHT_TEST) $(BUILD)/LINUX-I386-HELLO.ELF $(BUILD)/zenpkg $(BUILD)/HELLO.ZEX tests/zenpkg_foreign_test.sh tests/zenpkg_streaming_test.sh $(ZENPKG_FOREIGN_HOST_SRC) $(ZENPKG_FOREIGN_FORMAT_SRC) $(ZENPKG_COMPAT_PREFLIGHT_SRC) $(ZENPKG_SHA256_SRC)
	@rm -rf $(ZENPKG_FOREIGN_CHECK_OUT)
	@mkdir -p $(ZENPKG_FOREIGN_CHECK_OUT)
	$(ZENPKG_SHA256_TEST)
	$(LINUX_I386_ABI_TEST)
	$(LINUX_I386_ELF_TEST) $(BUILD)/LINUX-I386-HELLO.ELF
	$(ZENPKG_COMPAT_PREFLIGHT_TEST)
	$(ZENPKG_FOREIGN_FORMAT_TEST)
	$(ZENPKG_NATIVE_SNAPSHOT_TEST) $(BUILD)/HELLO.ZEX $(ZENPKG_FOREIGN_CHECK_OUT)/snapshot
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

$(POLICY_JOURNAL_TEST): tests/security_policy_transaction_test.cpp $(POLICY_JOURNAL_SRC) $(ZENPKG_SHA256_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(POLICY_JOURNAL_FIXTURE): tools/zenovfs_policy_journal_fixture.cpp $(ZENPKG_SHA256_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(POLICY_JOURNAL_HOT_IMAGE): all $(POLICY_JOURNAL_FIXTURE)
	$(POLICY_JOURNAL_FIXTURE) --hot-zmid $(BUILD)/zenov-data.img $@
	$(BUILD)/zenovfs-verify $@

$(POLICY_JOURNAL_CORRUPT_IMAGE): $(POLICY_JOURNAL_HOT_IMAGE) $(POLICY_JOURNAL_FIXTURE)
	$(POLICY_JOURNAL_FIXTURE) --corrupt-journal $(POLICY_JOURNAL_HOT_IMAGE) $@
	$(BUILD)/zenovfs-verify $@

$(POLICY_JOURNAL_QEMU_STAMP): all $(POLICY_JOURNAL_HOT_IMAGE) $(POLICY_JOURNAL_CORRUPT_IMAGE) tests/qemu_policy_journal.sh
	@rm -rf $(POLICY_JOURNAL_QEMU_OUT)
	@mkdir -p $(POLICY_JOURNAL_QEMU_OUT)
	bash tests/qemu_policy_journal.sh \
	  $(BUILD)/zenov-os.img \
	  $(POLICY_JOURNAL_HOT_IMAGE) \
	  $(POLICY_JOURNAL_CORRUPT_IMAGE) \
	  $(POLICY_JOURNAL_QEMU_OUT)
	$(BUILD)/zenovfs-verify $(POLICY_JOURNAL_QEMU_OUT)/hot-runtime.img
	$(BUILD)/zenovfs-verify $(POLICY_JOURNAL_QEMU_OUT)/corrupt-runtime.img
	@grep -Fq 'POLICY_TRANSACTION_QEMU_OK hot=replayed domain=ZMID policy=1 version=1 audit=restored journal=cleared reboot=clean corrupt=fail-closed' $(POLICY_JOURNAL_QEMU_OUT)/summary.log
	@touch $@

policy-journal-qemu: $(POLICY_JOURNAL_QEMU_STAMP)
qemu: policy-journal-qemu

$(TPM2_PROTOCOL_TEST): tests/tpm2_protocol_test.cpp kernel/parts/tpm2_protocol.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

$(TPM2_QEMU_STAMP): all tests/qemu_tpm2_nv.sh
	@rm -rf $(TPM2_QEMU_OUT)
	@mkdir -p $(TPM2_QEMU_OUT)
	bash tests/qemu_tpm2_nv.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  $(TPM2_QEMU_OUT)
	@grep -Fq 'TPM2_NV_QEMU_OK tis=mmio locality=0 provision=explicit counter=1-2-3 reboot=persistent absent=compatible' $(TPM2_QEMU_OUT)/summary.log
	@touch $@

tpm2-qemu: $(TPM2_QEMU_STAMP)

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

$(ZENOVFS_RESULT_TEST): tests/storage_fs_result_test.cpp kernel/parts/storage_block_result.inc kernel/parts/storage_fs_result.inc kernel/parts/storage_fs_state.inc kernel/parts/process_fs_errors.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

check: $(ATA_POLICY_TEST) $(ATA_RECOVERY_POLICY_TEST) $(BLOCK_RESULT_TEST) $(BLOCK_DEVICE_ABI_TEST) $(ZENOVFS_RESULT_TEST) $(POLICY_JOURNAL_TEST) $(TPM2_PROTOCOL_TEST)

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

$(ATA_READ_FAULT_STAMP): all tests/qemu_ata_read_faults.sh tests/blkdebug/ata-read-eio-once.conf tests/blkdebug/ata-read-eio-always.conf $(BUILD)/zenovfs-verify
	@rm -rf $(ATA_READ_FAULT_OUT)
	@mkdir -p $(ATA_READ_FAULT_OUT)
	bash tests/qemu_ata_read_faults.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  tests/blkdebug/ata-read-eio-once.conf \
	  tests/blkdebug/ata-read-eio-always.conf \
	  $(ATA_READ_FAULT_OUT)
	$(BUILD)/zenovfs-verify $(ATA_READ_FAULT_OUT)/recovered/runtime.img
	$(BUILD)/zenovfs-verify $(ATA_READ_FAULT_OUT)/exhausted/runtime.img
	@touch $@

ata-read-fault-qemu: $(ATA_READ_FAULT_STAMP)

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

include fs_surface.mk

SCHEDULER_POLICY_TEST := $(BUILD)/scheduler-policy-test
$(SCHEDULER_POLICY_TEST): tests/scheduler_policy_test.cpp kernel/parts/scheduler_policy.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@
check: $(SCHEDULER_POLICY_TEST)

SCHEDULER_QEMU_OUT := $(BUILD)/qemu/scheduler
SCHEDULER_QEMU_STAMP := $(SCHEDULER_QEMU_OUT)/.stamp
$(SCHEDULER_QEMU_STAMP): all tests/qemu_scheduler.sh
	@rm -rf $(SCHEDULER_QEMU_OUT)
	@mkdir -p $(SCHEDULER_QEMU_OUT)
	bash tests/qemu_scheduler.sh $(BUILD)/zenov-os.img $(BUILD)/zenov-data.img $(SCHEDULER_QEMU_OUT)
	@touch $@
scheduler-qemu: $(SCHEDULER_QEMU_STAMP)
