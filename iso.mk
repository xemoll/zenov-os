ISO_IMAGE ?= $(BUILD)/ZenovOS-0.1.1-x86.iso
ISO_ROOT ?= $(BUILD)/iso-root
ISO_QEMU_OUT ?= $(BUILD)/qemu/iso
ISO_LIVE_OUT ?= $(BUILD)/qemu/iso-live
ISO_DETERMINISTIC_DIR ?= /tmp/zenov-os-iso-deterministic
LIVE_IMAGE_PACKER ?= $(BUILD)/live-image-pack
LIVE_IMAGE_HEADER ?= $(BUILD)/generated/live_zenovfs.hpp
LIVE_IMAGE_REBUILD ?= /tmp/zenov-os-live-zenovfs.hpp

.PHONY: iso live-image-check iso-qemu iso-live-session iso-deterministic \
  iso-check vm-check

$(LIVE_IMAGE_PACKER): tools/live_image_pack.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(LIVE_IMAGE_HEADER): $(BUILD)/zenov-data.img $(LIVE_IMAGE_PACKER) | $(BUILD)
	@mkdir -p $(dir $@)
	$(LIVE_IMAGE_PACKER) $(BUILD)/zenov-data.img $@

# Extend the existing kernel rule without duplicating its recipe. The canonical
# ZenovFS image is generated first, packed deterministically, and compiled into
# the boot kernel as the Live ISO base layer.
$(BUILD)/kernel.o: $(LIVE_IMAGE_HEADER)

$(ISO_IMAGE): $(BUILD)/zenov-os.img tools/build_iso.sh packaging/ISO-README.txt
	bash tools/build_iso.sh $(BUILD)/zenov-os.img $(ISO_IMAGE) $(ISO_ROOT)

iso: $(ISO_IMAGE)

live-image-check: $(LIVE_IMAGE_HEADER) $(BUILD)/zenov-data.img $(LIVE_IMAGE_PACKER)
	@rm -f $(LIVE_IMAGE_REBUILD)
	$(LIVE_IMAGE_PACKER) $(BUILD)/zenov-data.img $(LIVE_IMAGE_REBUILD)
	cmp $(LIVE_IMAGE_HEADER) $(LIVE_IMAGE_REBUILD)
	@grep -Fq 'live_image_logical_sectors = 32768U' $(LIVE_IMAGE_HEADER)
	@echo 'Live ZenovFS packing: OK (16 MiB logical image, deterministic embedded sparse payload)'

iso-qemu: $(ISO_IMAGE) tests/qemu_iso_smoke.sh
	@rm -rf $(ISO_QEMU_OUT)
	@mkdir -p $(ISO_QEMU_OUT)
	bash tests/qemu_iso_smoke.sh $(ISO_IMAGE) $(ISO_QEMU_OUT)

iso-live-session: $(ISO_IMAGE) tests/qemu_iso_persistence.sh
	@rm -rf $(ISO_LIVE_OUT)
	@mkdir -p $(ISO_LIVE_OUT)
	bash tests/qemu_iso_persistence.sh $(ISO_IMAGE) $(ISO_LIVE_OUT)

iso-deterministic: $(ISO_IMAGE)
	@rm -rf $(ISO_DETERMINISTIC_DIR)
	@mkdir -p $(ISO_DETERMINISTIC_DIR)
	SOURCE_DATE_EPOCH=1784160000 bash tools/build_iso.sh \
	  $(BUILD)/zenov-os.img \
	  $(ISO_DETERMINISTIC_DIR)/ZenovOS-0.1.1-x86.iso \
	  $(ISO_DETERMINISTIC_DIR)/root
	cmp $(ISO_IMAGE) $(ISO_DETERMINISTIC_DIR)/ZenovOS-0.1.1-x86.iso
	@echo 'deterministic Live ISO rebuild: OK (embedded system, El Torito catalog and FAT12 image are byte-identical)'

iso-check: live-image-check iso iso-qemu iso-live-session iso-deterministic
	@echo 'ZenovOS Live ISO verification: OK (single ISO, no data disk, writable RAM session, desktop ready and deterministic rebuild)'

# Compatibility entrypoint for older CI callers. VM appliance generation is
# intentionally retired from the current line; the only launch artifact is ISO.
vm-check: iso-check
	@echo 'ZenovOS VM compatibility check: OK via self-contained ISO-only path'
