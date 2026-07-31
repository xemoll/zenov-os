FS_SURFACE_TEST := $(BUILD)/storage-fs-surface-test
FS_SURFACE_QEMU_OUT := $(BUILD)/qemu/fs-surface
FS_SURFACE_QEMU_STAMP := $(FS_SURFACE_QEMU_OUT)/.stamp

.PHONY: fs-surface-qemu

$(FS_SURFACE_TEST): tests/storage_fs_surface_test.cpp kernel/parts/storage_block_result.inc kernel/parts/storage_fs_result.inc kernel/parts/storage_fs_surface.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
	$@

check: $(FS_SURFACE_TEST)

$(FS_SURFACE_QEMU_STAMP): all tests/qemu_fs_surface.sh $(BUILD)/zenovfs-verify
	@rm -rf $(FS_SURFACE_QEMU_OUT)
	@mkdir -p $(FS_SURFACE_QEMU_OUT)
	bash tests/qemu_fs_surface.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  $(FS_SURFACE_QEMU_OUT)
	$(BUILD)/zenovfs-verify $(FS_SURFACE_QEMU_OUT)/runtime.img
	@touch $@

fs-surface-qemu: $(FS_SURFACE_QEMU_STAMP)

# Keep optical-image support isolated from the native build graph. GNUmakefile
# includes this file last, so these targets can reuse the verified raw image.
include iso.mk
