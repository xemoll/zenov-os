SHELL := /bin/bash
BUILD := build
HOST_CXX ?= g++
AS ?= as
LD ?= ld
OBJCOPY ?= objcopy
OPENSSL ?= openssl
KERNEL_MAX_BYTES := 524288

HOST_FLAGS := -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic
KERNEL_FLAGS := -m32 -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
  -fno-pic -fno-pie -fno-threadsafe-statics -fno-unwind-tables \
  -fno-asynchronous-unwind-tables -nostdlib -I$(BUILD)
KERNEL_PARTS := $(wildcard kernel/parts/*.inc)
ZENOV_STAGE0_SRC := tools/zenov_stage0.cpp $(wildcard tools/zenov_stage0/*.inc)
ZENOV_CONFIG_SRC := $(shell find kernel/config -type f -name '*.zv' -print 2>/dev/null | sort)
USER_ASM := user/hello.S user/fileio.S user/args.S user/console.S user/protect.S user/kernel_access.S
ZENOV_APP_EXPECTED_SHA256 := 9e1733af56a53ae31055b448f762815ba7a5e1a72be543aef325bd4ea36e0ad5
ZENOV_COMPILER_REVISION := a58c3419b09d46be7fc7180ba910c14033910fdf
ZGDB_V1_SHA256 := d2de9d9b05bcc61de82ab2b36149241ebb667782bcf3916dfc1d0b4fbcba360d
ZGDB_V2_SHA256 := 033307173932c452384033db5ca36ced4cc8cacf0efa5b328044f8c7772d8c6e
ZGDB_FILES := $(BUILD)/zenovguard-v1.zgdb $(BUILD)/zenovguard-v2.zgdb $(BUILD)/zenovguard-tampered.zgdb

.PHONY: all clean check test qemu deterministic inspect

all: $(BUILD)/zenov-os.img $(BUILD)/zenov-data.img $(BUILD)/build-manifest.json

$(BUILD):
	mkdir -p $(BUILD) $(BUILD)/generated

$(BUILD)/zenov-stage0: $(ZENOV_STAGE0_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tools/zenov_stage0.cpp -o $@

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

$(BUILD)/zenovfs-fault-test: tools/zenovfs_fault_test.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(BUILD)/zenov-app-compiler: tools/zenov_app_compiler.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(BUILD)/zgdb-builder: tools/zgdb_builder.cpp security/zgdb_crypto_material.hpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tools/zgdb_builder.cpp -o $@

$(BUILD)/zgdb.stamp: $(BUILD)/zgdb-builder security/zgdb_crypto_material.hpp security/zenovguard-root-public.pem | $(BUILD)
	$(BUILD)/zgdb-builder $(BUILD)/zenovguard-v1.zgdb $(BUILD)/zenovguard-v2.zgdb $(BUILD)/zenovguard-tampered.zgdb
	@printf '%s  %s\n' '$(ZGDB_V1_SHA256)' '$(BUILD)/zenovguard-v1.zgdb' | sha256sum -c -
	@printf '%s  %s\n' '$(ZGDB_V2_SHA256)' '$(BUILD)/zenovguard-v2.zgdb' | sha256sum -c -
	@head -c -256 $(BUILD)/zenovguard-v1.zgdb > $(BUILD)/zgdb-v1.signed
	@tail -c 256 $(BUILD)/zenovguard-v1.zgdb > $(BUILD)/zgdb-v1.signature
	@$(OPENSSL) dgst -sha256 -verify security/zenovguard-root-public.pem -signature $(BUILD)/zgdb-v1.signature $(BUILD)/zgdb-v1.signed
	@head -c -256 $(BUILD)/zenovguard-v2.zgdb > $(BUILD)/zgdb-v2.signed
	@tail -c 256 $(BUILD)/zenovguard-v2.zgdb > $(BUILD)/zgdb-v2.signature
	@$(OPENSSL) dgst -sha256 -verify security/zenovguard-root-public.pem -signature $(BUILD)/zgdb-v2.signature $(BUILD)/zgdb-v2.signed
	@head -c -256 $(BUILD)/zenovguard-tampered.zgdb > $(BUILD)/zgdb-tampered.signed
	@tail -c 256 $(BUILD)/zenovguard-tampered.zgdb > $(BUILD)/zgdb-tampered.signature
	@if $(OPENSSL) dgst -sha256 -verify security/zenovguard-root-public.pem -signature $(BUILD)/zgdb-tampered.signature $(BUILD)/zgdb-tampered.signed >/dev/null 2>&1; then echo 'tampered ZGDB signature unexpectedly verified' >&2; exit 1; fi
	@touch $@

$(ZGDB_FILES): $(BUILD)/zgdb.stamp
	@test -s $@

$(BUILD)/generated/zenov_config.hpp: kernel/main.zv $(ZENOV_CONFIG_SRC) $(BUILD)/zenov-stage0 | $(BUILD)
	$(BUILD)/zenov-stage0 kernel/main.zv -o $@

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

$(BUILD)/kernel.o: kernel/kernel.cpp $(KERNEL_PARTS) security/zgdb_crypto_material.hpp $(BUILD)/generated/zenov_config.hpp | $(BUILD)
	$(HOST_CXX) $(KERNEL_FLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/entry.o $(BUILD)/interrupts.o $(BUILD)/user-runtime.o $(BUILD)/kernel.o kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld -Map=$(BUILD)/kernel.map \
	  -o $@ $(BUILD)/entry.o $(BUILD)/interrupts.o $(BUILD)/user-runtime.o $(BUILD)/kernel.o
	@test -z "$$(nm -u $@)"

$(BUILD)/KERNEL.BIN: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(stat -c%s $@)" -gt 0
	@test "$$(stat -c%s $@)" -le $(KERNEL_MAX_BYTES)

$(BUILD)/zenov-os.img: $(BUILD)/BOOT.BIN $(BUILD)/KERNEL.BIN $(BUILD)/fat12-builder $(BUILD)/image-verify
	$(BUILD)/fat12-builder $(BUILD)/BOOT.BIN $(BUILD)/KERNEL.BIN $@
	$(BUILD)/image-verify $@

$(BUILD)/hello-user.o: user/hello.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/fileio-user.o: user/fileio.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/args-user.o: user/args.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/console-user.o: user/console.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/protect-user.o: user/protect.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/kaccess-user.o: user/kernel_access.S | $(BUILD)
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

$(BUILD)/FILEIO.ELF: $(BUILD)/fileio-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"

$(BUILD)/ARGS.ELF: $(BUILD)/args-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"

$(BUILD)/CONSOLE.ELF: $(BUILD)/console-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"

$(BUILD)/PROTECT.ELF: $(BUILD)/protect-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"

$(BUILD)/KACCESS.ELF: $(BUILD)/kaccess-user.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -o $@ $<
	@test -z "$$(nm -u $@)"

$(BUILD)/ZENOVAPP.ZEX: user/hello_zenov.zv $(BUILD)/zenov-app-compiler
	$(BUILD)/zenov-app-compiler $< -o $@ --abi 0.1.1
	@test "$$(od -An -tc -N4 $@ | tr -d ' \n')" = "ZEX1"
	@printf '%s  %s\n' '$(ZENOV_APP_EXPECTED_SHA256)' '$@' | sha256sum -c -

USER_APPS := $(BUILD)/HELLO.ZEX $(BUILD)/FILEIO.ELF $(BUILD)/ARGS.ELF $(BUILD)/CONSOLE.ELF $(BUILD)/PROTECT.ELF $(BUILD)/KACCESS.ELF $(BUILD)/ZENOVAPP.ZEX

$(BUILD)/zenov-data.img: $(USER_APPS) $(ZGDB_FILES) $(BUILD)/zenovfs-builder $(BUILD)/zenovfs-verify
	$(BUILD)/zenovfs-builder $(USER_APPS) $(ZGDB_FILES) $@
	@test "$$(stat -c%s $@)" -eq 16777216
	@test "$$(od -An -tc -N8 $@ | tr -d ' \n')" = "ZENOVFS1"
	$(BUILD)/zenovfs-verify $@

$(BUILD)/build-manifest.json: $(BUILD)/zenov-os.img $(BUILD)/zenov-data.img $(USER_APPS) $(ZGDB_FILES) kernel/main.zv $(ZENOV_CONFIG_SRC) kernel/kernel.cpp kernel/entry.S kernel/interrupts.S kernel/user.S $(KERNEL_PARTS) tools/zenov_app_compiler.cpp tools/zgdb_builder.cpp security/zgdb_crypto_material.hpp security/zenovguard-root-public.pem
	@boot_hash="$$(sha256sum $(BUILD)/BOOT.BIN | cut -d' ' -f1)"; \
	 kernel_hash="$$(sha256sum $(BUILD)/KERNEL.BIN | cut -d' ' -f1)"; \
	 image_hash="$$(sha256sum $(BUILD)/zenov-os.img | cut -d' ' -f1)"; \
	 data_hash="$$(sha256sum $(BUILD)/zenov-data.img | cut -d' ' -f1)"; \
	 source_hash="$$(cat kernel/main.zv $(ZENOV_CONFIG_SRC) kernel/kernel.cpp $(KERNEL_PARTS) security/zgdb_crypto_material.hpp | sha256sum | cut -d' ' -f1)"; \
	 compiler_hash="$$(sha256sum tools/zenov_app_compiler.cpp | cut -d' ' -f1)"; \
	 zenov_app_hash="$$(sha256sum $(BUILD)/ZENOVAPP.ZEX | cut -d' ' -f1)"; \
	 zgdb_v1_hash="$$(sha256sum $(BUILD)/zenovguard-v1.zgdb | cut -d' ' -f1)"; \
	 zgdb_v2_hash="$$(sha256sum $(BUILD)/zenovguard-v2.zgdb | cut -d' ' -f1)"; \
	 root_hash="$$(sha256sum security/zenovguard-root-public.pem | cut -d' ' -f1)"; \
	 printf '%s\n' \
	 '{' \
	 '  "format": "zenov-os-build-v8",' \
	 '  "product": "ZenovOS",' \
	 '  "version": "0.1.1",' \
	 '  "target": "i686-zenov-none",' \
	 '  "boot_loader": "FAT12 BIOS segmented kernel loader",' \
	 '  "memory": "E820 PMM / page-granular user protection / reusable heap",' \
	 '  "graphics": "QEMU Standard VGA / Bochs VBE 800x600x32 / supervisor MMIO / software desktop",' \
	 '  "input": "PS2 keyboard and 3-byte PS2 mouse packets",' \
	 '  "persistent_storage": "ATA PIO / ZenovFS1 copy-on-write commit",' \
	 '  "security": "ZenovGuard final-read SHA-256 appraisal / signed ZGDB RSA-2048 policy / rollback state / revocation / quarantine",' \
	 '  "zgdb_schema": 1,' \
	 '  "zgdb_compiled_floor": 1,' \
	 "  \"zgdb_root_public_sha256\": \"$$root_hash\"," \
	 "  \"zgdb_v1_sha256\": \"$$zgdb_v1_hash\"," \
	 "  \"zgdb_v2_sha256\": \"$$zgdb_v2_hash\"," \
	 '  "application_abi": "ZEX1 + ELF32 ring3 / int 0x80 / argv / console input",' \
	 '  "zenov_app_abi": "0.1.1",' \
	 '  "zenov_repository_commit": "$(ZENOV_COMPILER_REVISION)",' \
	 '  "zenov_app_contract_sha256": "$(ZENOV_APP_EXPECTED_SHA256)",' \
	 "  \"zenov_app_compiler_sha256\": \"$$compiler_hash\"," \
	 "  \"zenov_source_sha256\": \"$$source_hash\"," \
	 '  "outputs": {' \
	 "    \"BOOT.BIN\": {\"bytes\": $$(stat -c%s $(BUILD)/BOOT.BIN), \"sha256\": \"$$boot_hash\"}," \
	 "    \"KERNEL.BIN\": {\"bytes\": $$(stat -c%s $(BUILD)/KERNEL.BIN), \"sha256\": \"$$kernel_hash\"}," \
	 "    \"zenov-os.img\": {\"bytes\": $$(stat -c%s $(BUILD)/zenov-os.img), \"sha256\": \"$$image_hash\"}," \
	 "    \"zenov-data.img\": {\"bytes\": $$(stat -c%s $(BUILD)/zenov-data.img), \"sha256\": \"$$data_hash\"}," \
	 "    \"ZENOVAPP.ZEX\": {\"bytes\": $$(stat -c%s $(BUILD)/ZENOVAPP.ZEX), \"sha256\": \"$$zenov_app_hash\"}" \
	 '  }' \
	 '}' > $@

check: $(BUILD)/zenov-stage0 $(BUILD)/image-verify $(BUILD)/zenovfs-verify $(BUILD)/zenovfs-fault-test all
	$(BUILD)/zenov-stage0 --self-test
	$(BUILD)/image-verify $(BUILD)/zenov-os.img
	$(BUILD)/zenovfs-verify $(BUILD)/zenov-data.img
	$(BUILD)/zenovfs-fault-test $(BUILD)/zenov-data.img
	@for app in $(BUILD)/FILEIO.ELF $(BUILD)/ARGS.ELF $(BUILD)/CONSOLE.ELF $(BUILD)/PROTECT.ELF $(BUILD)/KACCESS.ELF; do \
	  readelf -h $$app | grep -q 'ELF32'; readelf -h $$app | grep -q 'Intel 80386'; \
	  ! readelf -l $$app | grep -q 'RWE'; test "$$(readelf -l $$app | grep -c 'LOAD')" -eq 2; \
	done
	@! find . -path './build' -prune -o -name '*.py' -print | grep -q .
	@grep -q 'system_version("0.1.1")' kernel/config/system.zv
	@grep -q '"format": "zenov-os-build-v8"' $(BUILD)/build-manifest.json
	@grep -q '"zgdb_schema": 1' $(BUILD)/build-manifest.json
	@grep -q '"zgdb_v1_sha256": "$(ZGDB_V1_SHA256)"' $(BUILD)/build-manifest.json
	@grep -q '"zgdb_v2_sha256": "$(ZGDB_V2_SHA256)"' $(BUILD)/build-manifest.json
	@grep -q '"zenov_app_abi": "0.1.1"' $(BUILD)/build-manifest.json
	@grep -q '"zenov_repository_commit": "$(ZENOV_COMPILER_REVISION)"' $(BUILD)/build-manifest.json
	@echo 'static checks: OK (0.1.1 signed ZGDB, ZenovGuard, graphics, memory, ABI and transactional storage)'

qemu: all $(BUILD)/zenovfs-fault-test
	@mkdir -p $(BUILD)/qemu
	@cp $(BUILD)/zenov-data.img $(BUILD)/qemu/zenov-data-runtime.img
	$(BUILD)/zenovfs-fault-test $(BUILD)/zenov-data.img --emit-recovery $(BUILD)/qemu/zenov-data-recovery.img
	bash tests/qemu_smoke.sh $(BUILD)/zenov-os.img $(BUILD)/qemu/zenov-data-runtime.img $(BUILD)/qemu $(BUILD)/qemu/zenov-data-recovery.img

test: check qemu deterministic

deterministic: all
	@rm -rf /tmp/zenov-os-deterministic
	@$(MAKE) --no-print-directory BUILD=/tmp/zenov-os-deterministic all
	@diff -u $(BUILD)/build-manifest.json /tmp/zenov-os-deterministic/build-manifest.json
	@cmp $(BUILD)/zenov-data.img /tmp/zenov-os-deterministic/zenov-data.img
	@for app in $(USER_APPS); do cmp $$app /tmp/zenov-os-deterministic/$$(basename $$app); done
	@for db in $(ZGDB_FILES); do cmp $$db /tmp/zenov-os-deterministic/$$(basename $$db); done
	@echo 'deterministic rebuild: OK (system, signed policy, transactional data volume and seven apps are byte-identical)'

inspect: all
	readelf -h $(BUILD)/kernel.elf
	readelf -S $(BUILD)/kernel.elf
	for app in $(BUILD)/*.ELF; do readelf -h $$app; readelf -l $$app; done
	nm -n $(BUILD)/kernel.elf | head -180

clean:
	rm -rf $(BUILD)
