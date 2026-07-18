ZENPKG_SRC := tools/zenpkg/main.cpp $(wildcard tools/zenpkg/*.hpp)
ZENPKG_MANAGER_SRC := kernel/parts/package_format.inc kernel/parts/package_manager.inc \
  kernel/parts/crypto_sha256.inc $(wildcard kernel/parts/package_manager/*.inc)
ZENPKG_PACKAGES := $(BUILD)/hello-native-0.1.0.zpk $(BUILD)/hello-native-0.2.0.zpk $(BUILD)/hello-native-0.3.0.zpk
ZENPKG_TRUSTED_PACKAGES := $(BUILD)/hello-native-0.1.0.zpk $(BUILD)/hello-native-0.2.0.zpk
ZENPKG_DATA_STAMP := $(BUILD)/zenpkg-data.stamp
ZENPKG_CHECK_STAMP := $(BUILD)/zenpkg-check.stamp
ZENPKG_QEMU_STAMP := $(BUILD)/qemu/zenpkg-qemu.stamp

.PHONY: zenpkg-check zenpkg-qemu

all: $(ZENPKG_DATA_STAMP) $(BUILD)/zenpkg-manifest.json
check: zenpkg-check
qemu: zenpkg-qemu
$(BUILD)/build-manifest.json: $(ZENPKG_DATA_STAMP)
$(AUDIT_FAULT_STAMP): $(ZENPKG_DATA_STAMP)
$(BUILD)/kernel.o: $(wildcard kernel/parts/package_manager/*.inc) kernel/parts/package_format.inc kernel/parts/package_manager.inc

$(BUILD)/zenpkg: $(ZENPKG_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tools/zenpkg/main.cpp -o $@

$(BUILD)/zenovfs-package-seed: tools/zenovfs_package_seed.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(BUILD)/package-manager-host-test: tests/package_manager_host_test.cpp $(ZENPKG_MANAGER_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tests/package_manager_host_test.cpp -o $@

$(BUILD)/hello-native-0.1.0.zpk: packages/examples/hello-native-0.1.0.zpkgmanifest $(BUILD)/HELLO.ZEX $(BUILD)/zenpkg
	$(BUILD)/zenpkg pack --manifest $< --payload $(BUILD)/HELLO.ZEX --output $@

$(BUILD)/hello-native-0.2.0.zpk: packages/examples/hello-native-0.2.0.zpkgmanifest $(BUILD)/HELLO.ZEX $(BUILD)/zenpkg
	$(BUILD)/zenpkg pack --manifest $< --payload $(BUILD)/HELLO.ZEX --output $@

$(BUILD)/hello-native-0.3.0.zpk: packages/examples/hello-native-0.3.0.zpkgmanifest $(BUILD)/HELLO.ZEX $(BUILD)/zenpkg
	$(BUILD)/zenpkg pack --manifest $< --payload $(BUILD)/HELLO.ZEX --output $@

$(ZENPKG_DATA_STAMP): $(BUILD)/zenov-data.img $(ZENPKG_PACKAGES) $(BUILD)/zenovfs-package-seed $(BUILD)/zenovfs-verify $(BUILD)/zenovfs-audit-verify
	$(BUILD)/zenovfs-package-seed $(BUILD)/zenov-data.img $(ZENPKG_PACKAGES)
	$(BUILD)/zenovfs-verify $(BUILD)/zenov-data.img
	$(BUILD)/zenovfs-audit-verify $(BUILD)/zenov-data.img
	@touch $@

$(BUILD)/zenpkg-manifest.json: $(ZENPKG_PACKAGES) $(ZENPKG_DATA_STAMP) | $(BUILD)
	@v1="$$(sha256sum $(BUILD)/hello-native-0.1.0.zpk | cut -d' ' -f1)"; \
	 v2="$$(sha256sum $(BUILD)/hello-native-0.2.0.zpk | cut -d' ' -f1)"; \
	 v3="$$(sha256sum $(BUILD)/hello-native-0.3.0.zpk | cut -d' ' -f1)"; \
	 printf '%s\n' \
	 '{' \
	 '  "format": "zenov-pkg-bootstrap-v1",' \
	 '  "product": "ZenovOS",' \
	 '  "version": "0.1.1",' \
	 '  "authorization": "compiled exact metadata plus full-package and payload SHA-256",' \
	 '  "network_repositories": false,' \
	 '  "packages": {' \
	 "    \"hello-native-0.1.0.zpk\": {\"authorized\": true, \"sha256\": \"$$v1\"}," \
	 "    \"hello-native-0.2.0.zpk\": {\"authorized\": true, \"sha256\": \"$$v2\"}," \
	 "    \"hello-native-0.3.0.zpk\": {\"authorized\": false, \"purpose\": \"negative authorization fixture\", \"sha256\": \"$$v3\"}" \
	 '  }' \
	 '}' > $@

$(ZENPKG_CHECK_STAMP): $(BUILD)/package-manager-host-test $(BUILD)/zenovfs-package-seed $(BUILD)/zenpkg $(ZENPKG_PACKAGES) $(ZENPKG_DATA_STAMP)
	$(BUILD)/zenovfs-package-seed --self-test
	$(BUILD)/package-manager-host-test
	@for package in $(ZENPKG_PACKAGES); do $(BUILD)/zenpkg verify $$package; done
	@grep -q '"version": "0.1.1"' $(BUILD)/zenpkg-manifest.json
	@grep -q '"network_repositories": false' $(BUILD)/zenpkg-manifest.json
	@touch $@

zenpkg-check: $(ZENPKG_CHECK_STAMP)

$(ZENPKG_QEMU_STAMP): all tests/qemu_zenpkg.sh
	@mkdir -p $(BUILD)/qemu/zenpkg
	@cp $(BUILD)/zenov-data.img $(BUILD)/qemu/zenpkg-runtime.img
	bash tests/qemu_zenpkg.sh $(BUILD)/zenov-os.img $(BUILD)/qemu/zenpkg-runtime.img $(BUILD)/qemu/zenpkg
	@touch $@

zenpkg-qemu: $(ZENPKG_QEMU_STAMP)
