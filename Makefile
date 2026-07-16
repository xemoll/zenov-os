SHELL := /bin/bash
BUILD := build
HOST_CXX ?= g++
AS ?= as
LD ?= ld
OBJCOPY ?= objcopy

HOST_FLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic
KERNEL_FLAGS := -m32 -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
  -fno-pic -fno-pie -fno-threadsafe-statics -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -nostdlib -I$(BUILD)
KERNEL_PARTS := $(wildcard kernel/parts/*.inc)

.PHONY: all clean check test qemu deterministic inspect

all: $(BUILD)/zenov-os.img $(BUILD)/zenov-data.img $(BUILD)/build-manifest.json

$(BUILD):
	mkdir -p $(BUILD) $(BUILD)/generated

$(BUILD)/zenov-stage0: tools/zenov_stage0.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
$(BUILD)/fat12-builder: tools/fat12_builder.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
$(BUILD)/image-verify: tools/image_verify.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
$(BUILD)/zex-pack: tools/zex_pack.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
$(BUILD)/zenovfs-builder: tools/zenovfs_builder.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@
$(BUILD)/zenovfs-verify: tools/zenovfs_verify.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(BUILD)/generated/zenov_config.hpp: kernel/main.zv $(BUILD)/zenov-stage0 | $(BUILD)
	$(BUILD)/zenov-stage0 $< -o $@

$(BUILD)/boot.o: boot/boot.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/BOOT.BIN: $(BUILD)/boot.o boot/boot.ld
	$(LD) -m elf_i386 -T boot/boot.ld $< -o $@
	@test "$$(stat -c%s $@)" -eq 512
	@test "$$(od -An -tx1 -j510 -N2 $@ | tr -d ' \n')" = "55aa"

$(BUILD)/entry.o: kernel/entry.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/interrupts.o: kernel/interrupts.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/user-runtime.o: kernel/user.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/kernel.o: kernel/kernel.cpp $(KERNEL_PARTS) $(BUILD)/generated/zenov_config.hpp | $(BUILD)
	$(HOST_CXX) $(KERNEL_FLAGS) -c $< -o $@
$(BUILD)/kernel.elf: $(BUILD)/entry.o $(BUILD)/interrupts.o $(BUILD)/user-runtime.o $(BUILD)/kernel.o kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld -Map=$(BUILD)/kernel.map \
	  -o $@ $(BUILD)/entry.o $(BUILD)/interrupts.o $(BUILD)/user-runtime.o $(BUILD)/kernel.o
	@test -z "$$(nm -u $@)"
$(BUILD)/KERNEL.BIN: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(stat -c%s $@)" -gt 0
	@test "$$(stat -c%s $@)" -le 61440
$(BUILD)/zenov-os.img: $(BUILD)/BOOT.BIN $(BUILD)/KERNEL.BIN $(BUILD)/fat12-builder $(BUILD)/image-verify
	$(BUILD)/fat12-builder $(BUILD)/BOOT.BIN $(BUILD)/KERNEL.BIN $@
	$(BUILD)/image-verify $@

$(BUILD)/hello-user.o: user/hello.S user/sdk/syscalls.inc | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/hello-user.elf: $(BUILD)/hello-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"
$(BUILD)/hello-user.bin: $(BUILD)/hello-user.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(stat -c%s $@)" -gt 0
$(BUILD)/HELLO.ZEX: $(BUILD)/hello-user.bin $(BUILD)/zex-pack
	$(BUILD)/zex-pack $< $@
	@test "$$(od -An -tc -N4 $@ | tr -d ' \n')" = "ZEX1"

$(BUILD)/fileio-user.o: user/fileio.S user/sdk/syscalls.inc | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/FILEIO.ELF: $(BUILD)/fileio-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"
	@readelf -h $@ | grep -q 'ELF32'
	@readelf -h $@ | grep -q 'Intel 80386'

$(BUILD)/fault-user.o: user/fault.S user/sdk/syscalls.inc | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/FAULT.ELF: $(BUILD)/fault-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"
	@readelf -h $@ | grep -q 'ELF32'
	@readelf -h $@ | grep -q 'Intel 80386'

$(BUILD)/zenov-data.img: $(BUILD)/HELLO.ZEX $(BUILD)/FILEIO.ELF $(BUILD)/FAULT.ELF $(BUILD)/zenovfs-builder $(BUILD)/zenovfs-verify
	$(BUILD)/zenovfs-builder $(BUILD)/HELLO.ZEX $(BUILD)/FILEIO.ELF $(BUILD)/FAULT.ELF $@
	@test "$$(stat -c%s $@)" -eq 16777216
	@test "$$(od -An -tc -N8 $@ | tr -d ' \n')" = "ZENOVFS1"
	$(BUILD)/zenovfs-verify $@

$(BUILD)/build-manifest.json: $(BUILD)/zenov-os.img $(BUILD)/zenov-data.img $(BUILD)/HELLO.ZEX $(BUILD)/FILEIO.ELF $(BUILD)/FAULT.ELF kernel/main.zv kernel/kernel.cpp kernel/entry.S kernel/interrupts.S kernel/user.S $(KERNEL_PARTS)
	@boot_hash="$$(sha256sum $(BUILD)/BOOT.BIN | cut -d' ' -f1)"; \
	 kernel_hash="$$(sha256sum $(BUILD)/KERNEL.BIN | cut -d' ' -f1)"; \
	 elf_hash="$$(sha256sum $(BUILD)/kernel.elf | cut -d' ' -f1)"; \
	 image_hash="$$(sha256sum $(BUILD)/zenov-os.img | cut -d' ' -f1)"; \
	 data_hash="$$(sha256sum $(BUILD)/zenov-data.img | cut -d' ' -f1)"; \
	 zex_hash="$$(sha256sum $(BUILD)/HELLO.ZEX | cut -d' ' -f1)"; \
	 fileio_hash="$$(sha256sum $(BUILD)/FILEIO.ELF | cut -d' ' -f1)"; \
	 fault_hash="$$(sha256sum $(BUILD)/FAULT.ELF | cut -d' ' -f1)"; \
	 source_hash="$$(sha256sum kernel/main.zv | cut -d' ' -f1)"; \
	 printf '%s\n' \
	 '{' \
	 '  "format": "zenov-os-build-v5",' \
	 '  "product": "ZenovOS",' \
	 '  "version": "0.1.1",' \
	 '  "revision": 2,' \
	 '  "target": "i686-zenov-none",' \
	 '  "python_runtime": false,' \
	 '  "memory": "E820 PMM / 4 KiB paging",' \
	 '  "persistent_storage": "ATA PIO / ZenovFS1",' \
	 '  "application_abi": "ZEX1 + ELF32 ring3 / int 0x80",' \
	 '  "fault_isolation": "ring3 exceptions return to kernel console",' \
	 "  \"zenov_source_sha256\": \"$$source_hash\"," \
	 '  "outputs": {' \
	 "    \"BOOT.BIN\": {\"bytes\": $$(stat -c%s $(BUILD)/BOOT.BIN), \"sha256\": \"$$boot_hash\"}," \
	 "    \"KERNEL.BIN\": {\"bytes\": $$(stat -c%s $(BUILD)/KERNEL.BIN), \"sha256\": \"$$kernel_hash\"}," \
	 "    \"kernel.elf\": {\"bytes\": $$(stat -c%s $(BUILD)/kernel.elf), \"sha256\": \"$$elf_hash\"}," \
	 "    \"zenov-os.img\": {\"bytes\": $$(stat -c%s $(BUILD)/zenov-os.img), \"sha256\": \"$$image_hash\"}," \
	 "    \"zenov-data.img\": {\"bytes\": $$(stat -c%s $(BUILD)/zenov-data.img), \"sha256\": \"$$data_hash\"}," \
	 "    \"HELLO.ZEX\": {\"bytes\": $$(stat -c%s $(BUILD)/HELLO.ZEX), \"sha256\": \"$$zex_hash\"}," \
	 "    \"FILEIO.ELF\": {\"bytes\": $$(stat -c%s $(BUILD)/FILEIO.ELF), \"sha256\": \"$$fileio_hash\"}," \
	 "    \"FAULT.ELF\": {\"bytes\": $$(stat -c%s $(BUILD)/FAULT.ELF), \"sha256\": \"$$fault_hash\"}" \
	 '  }' \
	 '}' > $@

check: $(BUILD)/zenov-stage0 $(BUILD)/image-verify $(BUILD)/zenovfs-verify all
	$(BUILD)/zenov-stage0 --self-test
	$(BUILD)/image-verify $(BUILD)/zenov-os.img
	$(BUILD)/zenovfs-verify $(BUILD)/zenov-data.img
	@cp $(BUILD)/zenov-data.img /tmp/zenov-data-corrupt.img
	@printf '\000' | dd of=/tmp/zenov-data-corrupt.img bs=1 seek=0 count=1 conv=notrunc status=none
	@! $(BUILD)/zenovfs-verify /tmp/zenov-data-corrupt.img >/dev/null 2>&1
	@! find . -path './build' -prune -o -name '*.py' -print | grep -q .
	@grep -q 'system_version("0.1.1")' kernel/main.zv
	@grep -q '"revision": 2' $(BUILD)/build-manifest.json
	@grep -q '"fault_isolation": "ring3 exceptions return to kernel console"' $(BUILD)/build-manifest.json
	@echo 'static checks: OK (0.1.1 revision 2 paging, persistence and user-fault isolation)'

qemu: all
	@mkdir -p $(BUILD)/qemu
	@cp $(BUILD)/zenov-data.img $(BUILD)/qemu/zenov-data-runtime.img
	bash tests/qemu_smoke.sh $(BUILD)/zenov-os.img $(BUILD)/qemu/zenov-data-runtime.img $(BUILD)/qemu

test: check qemu deterministic

deterministic: all
	@rm -rf /tmp/zenov-os-deterministic
	@$(MAKE) --no-print-directory BUILD=/tmp/zenov-os-deterministic all
	@diff -u $(BUILD)/build-manifest.json /tmp/zenov-os-deterministic/build-manifest.json
	@cmp $(BUILD)/zenov-os.img /tmp/zenov-os-deterministic/zenov-os.img
	@cmp $(BUILD)/zenov-data.img /tmp/zenov-os-deterministic/zenov-data.img
	@cmp $(BUILD)/HELLO.ZEX /tmp/zenov-os-deterministic/HELLO.ZEX
	@cmp $(BUILD)/FILEIO.ELF /tmp/zenov-os-deterministic/FILEIO.ELF
	@cmp $(BUILD)/FAULT.ELF /tmp/zenov-os-deterministic/FAULT.ELF
	@echo 'deterministic rebuild: OK (system, data and three applications are byte-identical)'

inspect: all
	readelf -h $(BUILD)/kernel.elf
	readelf -S $(BUILD)/kernel.elf
	readelf -h $(BUILD)/hello-user.elf
	readelf -h $(BUILD)/FILEIO.ELF
	readelf -h $(BUILD)/FAULT.ELF
	nm -n $(BUILD)/kernel.elf | head -140

clean:
	rm -rf $(BUILD)
