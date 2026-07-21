ZENPKG_SRC := tools/zenpkg/main.cpp $(wildcard tools/zenpkg/*.hpp)
ZENREPO_SRC := tools/zenrepo/main.cpp $(wildcard tools/zenrepo/*.hpp) security/zenrepo_crypto_material.hpp
ZENPKG_MANAGER_SRC := kernel/parts/package_format.inc kernel/parts/package_repository.inc $(wildcard kernel/parts/package_repository/*.inc) \
  kernel/parts/package_manager.inc kernel/parts/crypto_sha256.inc kernel/parts/rsa_pss.inc \
  kernel/parts/process_package_capabilities.inc $(wildcard kernel/parts/package_manager/*.inc)
ZENPKG_PACKAGES := $(BUILD)/hello-native-0.1.0.zpk $(BUILD)/hello-native-0.2.0.zpk $(BUILD)/hello-native-0.3.0.zpk
ZENREPO_FIXTURE_MATERIALIZER := tools/zenrepo/materialize_fixtures.sh
ZENREPO_METADATA := \
  $(BUILD)/zenrepo-metadata/root-bootstrap.zrm \
  $(BUILD)/zenrepo-metadata/root.zrm \
  $(BUILD)/zenrepo-metadata/targets.zrm \
  $(BUILD)/zenrepo-metadata/native-apps.zrm \
  $(BUILD)/zenrepo-metadata/snapshot.zrm \
  $(BUILD)/zenrepo-metadata/timestamp.zrm
ZENPKG_DATA_STAMP := $(BUILD)/zenpkg-data.stamp
ZENPKG_CHECK_STAMP := $(BUILD)/zenpkg-check.stamp
ZENPKG_QEMU_STAMP := $(BUILD)/qemu/zenpkg-qemu.stamp
ZENPKG_TRANSPORT_QEMU_STAMP := $(BUILD)/qemu/zenpkg-transport-qemu.stamp
ZENPKG_TRANSPORT_FAULT_QEMU_STAMP := $(BUILD)/qemu/zenpkg-transport-fault-qemu.stamp

.PHONY: zenpkg-check zenpkg-qemu zenpkg-transport-qemu zenpkg-transport-fault-qemu zenrepo-check

all: $(ZENPKG_DATA_STAMP) $(BUILD)/zenpkg-manifest.json
check: zenpkg-check
qemu: zenpkg-qemu
$(BUILD)/build-manifest.json: $(ZENPKG_DATA_STAMP) $(ZENPKG_MANAGER_SRC) $(ZENREPO_FIXTURE_MATERIALIZER)
$(AUDIT_FAULT_STAMP): $(ZENPKG_DATA_STAMP)
$(BUILD)/kernel.o: $(ZENPKG_MANAGER_SRC) security/zenrepo_crypto_material.hpp

$(BUILD)/zenpkg: $(ZENPKG_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tools/zenpkg/main.cpp -o $@

$(BUILD)/zenrepo: $(ZENREPO_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tools/zenrepo/main.cpp -o $@

$(BUILD)/zenovfs-package-seed: tools/zenovfs_package_seed.cpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) $< -o $@

$(BUILD)/package-transport-journal-test: tests/package_transport_journal_test.cpp kernel/parts/package_manager/transport_format.inc | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tests/package_transport_journal_test.cpp -o $@

$(BUILD)/zenpkg-transport-fault-test: tools/zenpkg_transport_fault_test.cpp tools/zenov_audit_format.hpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tools/zenpkg_transport_fault_test.cpp -o $@

$(BUILD)/zenrepo-metadata/.stamp: $(ZENREPO_FIXTURE_MATERIALIZER) $(wildcard tools/zenrepo/fixtures/*.inc) | $(BUILD)
	bash $(ZENREPO_FIXTURE_MATERIALIZER) $(BUILD)/zenrepo-metadata
	@touch $@

$(ZENREPO_METADATA): $(BUILD)/zenrepo-metadata/.stamp


$(BUILD)/package-repository-kernel-test: tests/package_repository_kernel_test.cpp kernel/parts/package_repository.inc $(wildcard kernel/parts/package_repository/*.inc) kernel/parts/rsa_pss.inc security/zenrepo_crypto_material.hpp | $(BUILD)
	$(HOST_CXX) $(HOST_FLAGS) tests/package_repository_kernel_test.cpp -o $@

$(BUILD)/hello-native-0.1.0.zpk: packages/examples/hello-native-0.1.0.zpkgmanifest $(BUILD)/HELLO.ZEX $(BUILD)/zenpkg
	$(BUILD)/zenpkg pack --manifest $< --payload $(BUILD)/HELLO.ZEX --output $@

$(BUILD)/hello-native-0.2.0.zpk: packages/examples/hello-native-0.2.0.zpkgmanifest $(BUILD)/HELLO.ZEX $(BUILD)/zenpkg
	$(BUILD)/zenpkg pack --manifest $< --payload $(BUILD)/HELLO.ZEX --output $@

$(BUILD)/hello-native-0.3.0.zpk: packages/examples/hello-native-0.3.0.zpkgmanifest $(BUILD)/HELLO.ZEX $(BUILD)/zenpkg
	$(BUILD)/zenpkg pack --manifest $< --payload $(BUILD)/HELLO.ZEX --output $@

$(ZENPKG_DATA_STAMP): $(BUILD)/zenov-data.img $(ZENPKG_PACKAGES) $(ZENREPO_METADATA) $(BUILD)/zenovfs-package-seed $(BUILD)/zenovfs-verify $(BUILD)/zenovfs-audit-verify
	$(BUILD)/zenovfs-package-seed $(BUILD)/zenov-data.img $(ZENPKG_PACKAGES) $(ZENREPO_METADATA)
	$(BUILD)/zenovfs-verify $(BUILD)/zenov-data.img
	$(BUILD)/zenovfs-audit-verify $(BUILD)/zenov-data.img
	@touch $@

$(BUILD)/zenpkg-manifest.json: $(ZENPKG_PACKAGES) $(ZENREPO_METADATA) $(ZENPKG_DATA_STAMP) | $(BUILD)
	@v1="$$(sha256sum $(BUILD)/hello-native-0.1.0.zpk | cut -d' ' -f1)"; \
	 v2="$$(sha256sum $(BUILD)/hello-native-0.2.0.zpk | cut -d' ' -f1)"; \
	 v3="$$(sha256sum $(BUILD)/hello-native-0.3.0.zpk | cut -d' ' -f1)"; \
	 timestamp="$$(sha256sum $(BUILD)/zenrepo-metadata/timestamp.zrm | cut -d' ' -f1)"; \
	 printf '%s\n' \
	 '{' \
	 '  "format": "zenov-pkg-offline-repository",' \
	 '  "product": "ZenovOS",' \
	 '  "version": "0.1.1",' \
	 '  "authorization": "signed root targets snapshot timestamp and native delegation",' \
	 '  "network_repositories": false,' \
	 "  \"timestamp_sha256\": \"$$timestamp\"," \
	 '  "packages": {' \
	 "    \"hello-native-0.1.0.zpk\": {\"authorized\": true, \"sha256\": \"$$v1\"}," \
	 "    \"hello-native-0.2.0.zpk\": {\"authorized\": true, \"sha256\": \"$$v2\"}," \
	 "    \"hello-native-0.3.0.zpk\": {\"authorized\": false, \"purpose\": \"negative authorization fixture\", \"sha256\": \"$$v3\"}" \
	 '  }' \
	 '}' > $@

zenrepo-check: $(BUILD)/zenrepo
	bash tests/zenrepo_test.sh

$(ZENPKG_CHECK_STAMP): $(BUILD)/package-repository-kernel-test $(BUILD)/package-transport-journal-test $(BUILD)/zenpkg-transport-fault-test $(BUILD)/zenovfs-package-seed $(BUILD)/zenpkg $(BUILD)/zenrepo $(ZENPKG_PACKAGES) $(ZENPKG_DATA_STAMP)
	$(BUILD)/zenovfs-package-seed --self-test
	bash tests/zenrepo_test.sh
	$(BUILD)/package-repository-kernel-test $(BUILD)/zenrepo-test/fixtures
	$(BUILD)/package-transport-journal-test
	$(BUILD)/zenpkg-transport-fault-test $(BUILD)/zenov-data.img
	@for package in $(ZENPKG_PACKAGES); do $(BUILD)/zenpkg verify $$package; done
	@grep -q '"version": "0.1.1"' $(BUILD)/zenpkg-manifest.json
	@grep -q '"network_repositories": false' $(BUILD)/zenpkg-manifest.json
	@! grep -q 'metadata_versions' $(BUILD)/zenpkg-manifest.json
	@touch $@

zenpkg-check: $(ZENPKG_CHECK_STAMP)

$(ZENPKG_QEMU_STAMP): all tests/qemu_zenpkg.sh
	@mkdir -p $(BUILD)/qemu/zenpkg
	@cp $(BUILD)/zenov-data.img $(BUILD)/qemu/zenpkg-runtime.img
	bash tests/qemu_zenpkg.sh $(BUILD)/zenov-os.img $(BUILD)/qemu/zenpkg-runtime.img $(BUILD)/qemu/zenpkg
	@touch $@

zenpkg-qemu: $(ZENPKG_QEMU_STAMP)

$(ZENPKG_TRANSPORT_QEMU_STAMP): all tests/qemu_zenpkg_transport_recovery.sh
	@mkdir -p $(BUILD)/qemu/zenpkg-transport
	@cp $(BUILD)/zenov-data.img $(BUILD)/qemu/zenpkg-transport-runtime.img
	bash tests/qemu_zenpkg_transport_recovery.sh $(BUILD)/zenov-os.img $(BUILD)/qemu/zenpkg-transport-runtime.img $(BUILD)/qemu/zenpkg-transport
	@touch $@

zenpkg-transport-qemu: $(ZENPKG_TRANSPORT_QEMU_STAMP)

$(ZENPKG_TRANSPORT_FAULT_QEMU_STAMP): all $(BUILD)/zenpkg-transport-fault-test tests/qemu_zenpkg_transport_faults.sh
	@mkdir -p $(BUILD)/qemu/zenpkg-faults
	$(BUILD)/zenpkg-transport-fault-test $(BUILD)/zenov-data.img \
	  --emit-invalid-journal $(BUILD)/qemu/zenpkg-fault-invalid-journal.img \
	  --emit-commit-recovery $(BUILD)/qemu/zenpkg-fault-commit-recovery.img \
	  --emit-corrupt-final $(BUILD)/qemu/zenpkg-fault-corrupt-final.img
	bash tests/qemu_zenpkg_transport_faults.sh $(BUILD)/zenov-os.img \
	  $(BUILD)/qemu/zenpkg-fault-invalid-journal.img \
	  $(BUILD)/qemu/zenpkg-fault-commit-recovery.img \
	  $(BUILD)/qemu/zenpkg-fault-corrupt-final.img \
	  $(BUILD)/qemu/zenpkg-faults
	@touch $@

zenpkg-transport-fault-qemu: $(ZENPKG_TRANSPORT_FAULT_QEMU_STAMP)
