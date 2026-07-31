ISO_IMAGE ?= $(BUILD)/ZenovOS-0.1.1-x86.iso
ISO_ROOT ?= $(BUILD)/iso-root
ISO_QEMU_OUT ?= $(BUILD)/qemu/iso
ISO_DETERMINISTIC_DIR ?= /tmp/zenov-os-iso-deterministic

.PHONY: iso iso-qemu iso-deterministic iso-check

$(ISO_IMAGE): $(BUILD)/zenov-os.img tools/build_iso.sh packaging/ISO-README.txt
	bash tools/build_iso.sh $(BUILD)/zenov-os.img $(ISO_IMAGE) $(ISO_ROOT)

iso: $(ISO_IMAGE)

iso-qemu: $(ISO_IMAGE) $(BUILD)/zenov-data.img tests/qemu_iso_smoke.sh
	@rm -rf $(ISO_QEMU_OUT)
	@mkdir -p $(ISO_QEMU_OUT)
	bash tests/qemu_iso_smoke.sh $(ISO_IMAGE) $(BUILD)/zenov-data.img $(ISO_QEMU_OUT)

iso-deterministic: $(ISO_IMAGE)
	@rm -rf $(ISO_DETERMINISTIC_DIR)
	@mkdir -p $(ISO_DETERMINISTIC_DIR)
	SOURCE_DATE_EPOCH=1784160000 bash tools/build_iso.sh \
	  $(BUILD)/zenov-os.img \
	  $(ISO_DETERMINISTIC_DIR)/ZenovOS-0.1.1-x86.iso \
	  $(ISO_DETERMINISTIC_DIR)/root
	cmp $(ISO_IMAGE) $(ISO_DETERMINISTIC_DIR)/ZenovOS-0.1.1-x86.iso
	@echo 'deterministic ISO rebuild: OK (El Torito catalog and embedded FAT12 image are byte-identical)'

iso-check: iso iso-qemu iso-deterministic
	@echo 'ZenovOS ISO verification: OK (BIOS El Torito boot, ZenovFS mount and deterministic rebuild)'
