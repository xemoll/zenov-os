ISO_IMAGE ?= $(BUILD)/ZenovOS-0.1.1-x86.iso
ISO_ROOT ?= $(BUILD)/iso-root
ISO_QEMU_OUT ?= $(BUILD)/qemu/iso
ISO_PERSISTENCE_OUT ?= $(BUILD)/qemu/iso-persistence
ISO_DETERMINISTIC_DIR ?= /tmp/zenov-os-iso-deterministic
VM_APPLIANCE_DIR ?= $(BUILD)/vm-appliances
VM_APPLIANCE_REBUILD_DIR ?= /tmp/zenov-os-vm-appliances-rebuild
VM_APPLIANCE_STAMP ?= $(VM_APPLIANCE_DIR)/.verified
VM_LIFECYCLE_OUT ?= $(BUILD)/vm-lifecycle-test
VM_LAUNCHER_OUT ?= $(BUILD)/vm-launcher-test
VM_DIST ?= dist-vm

.PHONY: iso iso-qemu iso-persistence iso-deterministic iso-check \
  vm-appliances vm-appliances-semantic vm-lifecycle-check vm-launcher-check \
  vm-package vm-check

$(ISO_IMAGE): $(BUILD)/zenov-os.img tools/build_iso.sh packaging/ISO-README.txt
	bash tools/build_iso.sh $(BUILD)/zenov-os.img $(ISO_IMAGE) $(ISO_ROOT)

iso: $(ISO_IMAGE)

iso-qemu: $(ISO_IMAGE) $(BUILD)/zenov-data.img tests/qemu_iso_smoke.sh
	@rm -rf $(ISO_QEMU_OUT)
	@mkdir -p $(ISO_QEMU_OUT)
	bash tests/qemu_iso_smoke.sh $(ISO_IMAGE) $(BUILD)/zenov-data.img $(ISO_QEMU_OUT)

iso-persistence: $(ISO_IMAGE) $(BUILD)/zenov-data.img $(BUILD)/zenovfs-verify tests/qemu_iso_persistence.sh
	@rm -rf $(ISO_PERSISTENCE_OUT)
	@mkdir -p $(ISO_PERSISTENCE_OUT)
	bash tests/qemu_iso_persistence.sh $(ISO_IMAGE) $(BUILD)/zenov-data.img $(ISO_PERSISTENCE_OUT)

iso-deterministic: $(ISO_IMAGE)
	@rm -rf $(ISO_DETERMINISTIC_DIR)
	@mkdir -p $(ISO_DETERMINISTIC_DIR)
	SOURCE_DATE_EPOCH=1784160000 bash tools/build_iso.sh \
	  $(BUILD)/zenov-os.img \
	  $(ISO_DETERMINISTIC_DIR)/ZenovOS-0.1.1-x86.iso \
	  $(ISO_DETERMINISTIC_DIR)/root
	cmp $(ISO_IMAGE) $(ISO_DETERMINISTIC_DIR)/ZenovOS-0.1.1-x86.iso
	@echo 'deterministic ISO rebuild: OK (El Torito catalog and embedded FAT12 image are byte-identical)'

iso-check: iso iso-qemu iso-persistence iso-deterministic
	@echo 'ZenovOS ISO verification: OK (BIOS El Torito boot, two-boot ZenovFS persistence and deterministic rebuild)'

$(VM_APPLIANCE_STAMP): $(ISO_IMAGE) $(BUILD)/zenov-data.img \
  tools/build_vm_appliances.sh tools/verify_vm_appliances.sh \
  packaging/prepare-vm.sh packaging/prepare-vm.ps1 \
  packaging/ZenovOS-0.1.1.vmx packaging/VM-QUICKSTART.txt
	bash tools/build_vm_appliances.sh $(BUILD)/zenov-data.img $(ISO_IMAGE) $(VM_APPLIANCE_DIR)
	bash tools/verify_vm_appliances.sh $(BUILD)/zenov-data.img $(ISO_IMAGE) $(VM_APPLIANCE_DIR)
	@touch $@

vm-appliances: $(VM_APPLIANCE_STAMP)

vm-appliances-semantic: $(VM_APPLIANCE_STAMP)
	@rm -rf $(VM_APPLIANCE_REBUILD_DIR)
	bash tools/build_vm_appliances.sh \
	  $(BUILD)/zenov-data.img \
	  $(ISO_IMAGE) \
	  $(VM_APPLIANCE_REBUILD_DIR)
	bash tools/verify_vm_appliances.sh \
	  $(BUILD)/zenov-data.img \
	  $(ISO_IMAGE) \
	  $(VM_APPLIANCE_REBUILD_DIR)
	cmp $(VM_APPLIANCE_DIR)/VM-APPLIANCE-MANIFEST.json $(VM_APPLIANCE_REBUILD_DIR)/VM-APPLIANCE-MANIFEST.json
	cmp $(VM_APPLIANCE_DIR)/VM-QUICKSTART.txt $(VM_APPLIANCE_REBUILD_DIR)/VM-QUICKSTART.txt
	cmp $(VM_APPLIANCE_DIR)/prepare-vm.sh $(VM_APPLIANCE_REBUILD_DIR)/prepare-vm.sh
	cmp $(VM_APPLIANCE_DIR)/prepare-vm.ps1 $(VM_APPLIANCE_REBUILD_DIR)/prepare-vm.ps1
	cmp $(VM_APPLIANCE_DIR)/ZenovOS-0.1.1.vmx $(VM_APPLIANCE_REBUILD_DIR)/ZenovOS-0.1.1.vmx
	@echo 'VM appliance semantic rebuild: OK (stable metadata and byte-identical guest-visible ZenovFS content)'

vm-lifecycle-check: $(BUILD)/zenov-data.img packaging/manage-vm.sh tests/vm_lifecycle_test.sh
	bash tests/vm_lifecycle_test.sh $(BUILD)/zenov-data.img $(VM_LIFECYCLE_OUT)

vm-launcher-check: packaging/prepare-vm.sh tests/vm_launcher_test.sh
	bash tests/vm_launcher_test.sh packaging/prepare-vm.sh $(VM_LAUNCHER_OUT)

vm-package: $(VM_APPLIANCE_STAMP) tools/package_vm_appliances.sh packaging/manage-vm.sh $(BUILD)/build-manifest.json
	bash tools/package_vm_appliances.sh \
	  $(BUILD)/zenov-os.img \
	  $(BUILD)/zenov-data.img \
	  $(ISO_IMAGE) \
	  $(VM_APPLIANCE_DIR) \
	  $(VM_DIST) \
	  $(BUILD)/build-manifest.json
	install -m 0755 packaging/manage-vm.sh $(VM_DIST)/manage-vm.sh
	@cd $(VM_DIST) && sha256sum manage-vm.sh >> SHA256SUMS.txt && sha256sum -c SHA256SUMS.txt
	@test "$$(find $(VM_DIST) -maxdepth 1 -type f | wc -l)" -eq 15
	@echo 'VM lifecycle manager packaged: OK assets=15'

vm-check: iso-check vm-appliances-semantic vm-lifecycle-check vm-launcher-check vm-package
	@echo 'ZenovOS VM verification: OK (optical boot, persistence, appliance roundtrip, transactional lifecycle, AMD-V-safe launcher and direct packaging)'
