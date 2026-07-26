#!/usr/bin/env bash
set -euo pipefail

: "${TARGET_BASE:?TARGET_BASE is required}"
readonly log_dir='ci-logs/zvrt-qemu-verification'
mkdir -p "$log_dir"

python3 - <<'PY'
from pathlib import Path

makefile = Path('Makefile')
text = makefile.read_text()
replacements = [
    ('.PHONY: all clean check test qemu deterministic inspect\n',
     '.PHONY: all clean check test qemu zvrt-qemu deterministic inspect\n'),
    ('tools/zrwp_builder.cpp tools/zrwp_verify.cpp tools/zenovfs_audit_verify.cpp',
     'tools/zrwp_builder.cpp tools/zrwp_verify.cpp tools/zvrt_builder.cpp tools/zvrt_verify.cpp tools/zenovfs_zvrt_verify.cpp tools/zenovfs_zvrt_corrupt.cpp tools/zenovfs_audit_verify.cpp'),
    ('security/zgdb_crypto_material.hpp security/zcap_crypto_material.hpp security/zmid_crypto_material.hpp security/zrwp_crypto_material.hpp security/zenovguard-root-public.pem security/zcap-root-public.pem security/zmid-root-public.pem security/zrwp-root-public.pem',
     'security/zgdb_crypto_material.hpp security/zcap_crypto_material.hpp security/zmid_crypto_material.hpp security/zrwp_crypto_material.hpp security/zvrt_crypto_material.hpp security/zenovguard-root-public.pem security/zcap-root-public.pem security/zmid-root-public.pem security/zrwp-root-public.pem security/zvrt-root-public.pem'),
    ('source_hash="$$(cat kernel/main.zv $(ZENOV_CONFIG_SRC) kernel/kernel.cpp $(KERNEL_PARTS) security/zgdb_crypto_material.hpp security/zcap_crypto_material.hpp security/zmid_crypto_material.hpp security/zrwp_crypto_material.hpp | sha256sum | cut -d\' \' -f1)";',
     'source_hash="$$(cat kernel/main.zv $(ZENOV_CONFIG_SRC) kernel/kernel.cpp $(KERNEL_PARTS) security/zgdb_crypto_material.hpp security/zcap_crypto_material.hpp security/zmid_crypto_material.hpp security/zrwp_crypto_material.hpp security/zvrt_crypto_material.hpp | sha256sum | cut -d\' \' -f1)";'),
    ('\t zrwp_root_hash="$$(sha256sum security/zrwp-root-public.pem | cut -d\' \' -f1)"; \\\n',
     '\t zrwp_root_hash="$$(sha256sum security/zrwp-root-public.pem | cut -d\' \' -f1)"; \\\n\t zvrt_v1_hash="$$(sha256sum $(BUILD)/verified-reads-v1.zvrt | cut -d\' \' -f1)"; \\\n\t zvrt_root_hash="$$(sha256sum security/zvrt-root-public.pem | cut -d\' \' -f1)"; \\\n'),
    ('ZGDB2 executable policy / ZCAP1 syscall policy / ZMID1 signed malware intelligence / ZRWP1 controlled-folder and behavior policy / on-write and synchronous on-access read prevention',
     'ZGDB2 executable policy / ZCAP1 syscall policy / ZMID1 signed malware intelligence / ZRWP1 controlled-folder and behavior policy / ZVRT1 authenticated reads / on-write and synchronous on-access read prevention'),
    ('\t \'  "on_access_read": "shell and ring-3 file reads / infected block and output scrub / suspicious durable audit / internal policy namespaces excluded",\' \\\n',
     '\t \'  "on_access_read": "shell and ring-3 file reads / infected block and output scrub / suspicious durable audit / internal policy namespaces excluded",\' \\\n\t \'  "zvrt_schema": 1,\' \\\n\t \'  "zvrt_compiled_floor": 1,\' \\\n\t \'  "zvrt_root_key_id": "d28215ec62269ffc",\' \\\n\t "  \\"zvrt_root_public_sha256\\": \\"$$zvrt_root_hash\\"," \\\n\t "  \\"zvrt_v1_sha256\\": \\"$$zvrt_v1_hash\\"," \\\n\t \'  "zvrt_chunk_bytes": 4096,\' \\\n\t \'  "zvrt_records": 4,\' \\\n\t \'  "zvrt_leaves": 5,\' \\\n\t \'  "authenticated_read": "ZenovFS checksum then signed path-size-chunk Merkle commitment before release or executable appraisal",\' \\\n'),
    ('\t@grep -q \'"on_access_read": "shell and ring-3 file reads / infected block and output scrub / suspicious durable audit / internal policy namespaces excluded"\' $(BUILD)/build-manifest.json\n',
     '\t@grep -q \'"on_access_read": "shell and ring-3 file reads / infected block and output scrub / suspicious durable audit / internal policy namespaces excluded"\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_schema": 1\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_compiled_floor": 1\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_root_key_id": "d28215ec62269ffc"\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_v1_sha256": "$(ZVRT_V1_SHA256)"\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_chunk_bytes": 4096\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_records": 4\' $(BUILD)/build-manifest.json\n\t@grep -q \'"zvrt_leaves": 5\' $(BUILD)/build-manifest.json\n'),
    ("\t@echo 'static checks: OK (0.1.1 ZGDB2 + ZCAP1 + ZMID1 + ZRWP1 RSA-PSS, ZGAL1 audit COW crash matrix, graphics, memory, ABI and transactional storage)'\n",
     "\t@echo 'static checks: OK (0.1.1 ZGDB2 + ZCAP1 + ZMID1 + ZRWP1 + ZVRT1 RSA-PSS, ZGAL1 audit COW crash matrix, graphics, memory, ABI and transactional storage)'\n"),
    ('test: check qemu deterministic\n', 'test: check qemu zvrt-qemu deterministic\n'),
    ('\t@for policy in $(ZGDB_FILES) $(ZCAP_FILES) $(ZMID_FILES) $(ZRWP_FILES); do cmp $$policy /tmp/zenov-os-deterministic/$$(basename $$policy); done\n',
     '\t@for policy in $(ZGDB_FILES) $(ZCAP_FILES) $(ZMID_FILES) $(ZRWP_FILES) $(ZVRT_FILES); do cmp $$policy /tmp/zenov-os-deterministic/$$(basename $$policy); done\n'),
    ("\t@echo 'deterministic rebuild: OK (system, ZGDB2/ZCAP1/ZMID1/ZRWP1 RSA-PSS policies, empty ZGAL1 seed, data volume and seven apps are byte-identical)'\n",
     "\t@echo 'deterministic rebuild: OK (system, ZGDB2/ZCAP1/ZMID1/ZRWP1/ZVRT1 RSA-PSS policies, empty ZGAL1 seed, data volume and seven apps are byte-identical)'\n"),
]
for old, new in replacements:
    count = text.count(old)
    assert count == 1, (old[:120], count)
    text = text.replace(old, new, 1)

qemu_anchor = "\t@echo 'persistent audit and antimalware verification: OK (runtime chain valid; signed ZMID update; on-access/read-write prevention and quarantine state verified)'\n\n"
zvrt_target = qemu_anchor + '''zvrt-qemu: all $(BUILD)/zenovfs-zvrt-verify $(BUILD)/zenovfs-audit-verify $(ZVRT_MANIFEST_CORRUPT_IMAGE) $(ZVRT_DATA_CORRUPT_IMAGE)
\t@mkdir -p $(BUILD)/qemu/zvrt
\tbash tests/qemu_zvrt.sh $(BUILD)/zenov-os.img $(BUILD)/zenov-data.img $(ZVRT_MANIFEST_CORRUPT_IMAGE) $(ZVRT_DATA_CORRUPT_IMAGE) $(BUILD)/qemu/zvrt
\t$(BUILD)/zenovfs-zvrt-verify $(BUILD)/qemu/zvrt/valid-runtime.img
\t$(BUILD)/zenovfs-audit-verify $(BUILD)/qemu/zvrt/data-corrupt-runtime.img --require-nonempty
\t@grep -Fq 'ZENOV_ZVRT_QEMU_OK valid=yes manifest_fail_closed=yes data_blocked=yes payload_disclosure=no multichunk=2 audit=durable' $(BUILD)/qemu/zvrt/summary.log
\t@echo 'ZVRT QEMU verification: OK (valid multichunk read; manifest fail-closed; checksum-repaired data blocked with durable audit)'

'''
assert text.count(qemu_anchor) == 1, text.count(qemu_anchor)
text = text.replace(qemu_anchor, zvrt_target, 1)
makefile.write_text(text)

readme = Path('README.md')
text = readme.read_text()
replacements = [
    ('signed ZRWP1 controlled-folder and mutation-budget policy, prevention before persistent writes',
     'signed ZRWP1 controlled-folder and mutation-budget policy, signed ZVRT1 authenticated reads, prevention before persistent writes'),
    ('signed ZRWP1 controlled-folder budgets, prevention before persistent writes and protected quarantine;',
     'signed ZRWP1 controlled-folder budgets, signed ZVRT1 path-bound Merkle commitments, prevention before persistent writes and protected quarantine;'),
    ('[`docs/RANSOMWARE_DEFENSE_0.1.1.md`](docs/RANSOMWARE_DEFENSE_0.1.1.md) and [`docs/AUDIT_JOURNAL_0.1.1.md`](docs/AUDIT_JOURNAL_0.1.1.md).',
     '[`docs/RANSOMWARE_DEFENSE_0.1.1.md`](docs/RANSOMWARE_DEFENSE_0.1.1.md), [`docs/VERIFIED_READS_0.1.1.md`](docs/VERIFIED_READS_0.1.1.md) and [`docs/AUDIT_JOURNAL_0.1.1.md`](docs/AUDIT_JOURNAL_0.1.1.md).'),
    ('and ZRWP1 supplies independently signed controlled-folder paths, exact writer identities and mutation budgets with audit/block modes.',
     'ZRWP1 supplies independently signed controlled-folder paths, exact writer identities and mutation budgets with audit/block modes, and ZVRT1 supplies signed path/size/chunk Merkle commitments for selected persistent objects.'),
    ('ZenovFS checksum-valid final read\n        │\n        ▼\nkernel SHA-256',
     'ZenovFS checksum-valid final read\n        │\n        ▼\nZVRT1 signed path/size/chunk Merkle commitment when protected\n        │\n        ▼\nkernel SHA-256'),
    ('`/security/ransomware-policy.zrwp` under root `7186b2bd819e47dc`.',
     '`/security/ransomware-policy.zrwp` under root `7186b2bd819e47dc`, and `/security/verified-reads.zvrt` under root `d28215ec62269ffc`.'),
    ('The trusted applications, active ZGDB2/ZCAP1/ZMID1/ZRWP1 policies, their version state,',
     'The trusted applications, active ZGDB2/ZCAP1/ZMID1/ZRWP1/ZVRT1 policies, their version state,'),
    ('Internal signed-policy, repository-state and quarantine reads remain on their dedicated parsers to prevent recursive appraisal.',
     'Internal signed-policy, repository-state and quarantine reads remain on their dedicated parsers to prevent recursive appraisal. Selected ordinary and executable paths additionally pass ZVRT1 after ZenovFS checksum verification; a mismatch is durably audited, scrubbed and returned as `checksum-mismatch` before ZMID, ZGDB or ZCAP receives the bytes.'),
    ('ZENOV_SECURITY_GATE_OK\n',
     'ZENOV_SECURITY_GATE_OK\nZVRT_ROOT_KEY_OK id=d28215ec62269ffc\nZVRT_PSS_SIGNATURE_OK\nZVRT_MANIFEST_OK version=1 records=4 chunk=4096 leaves=5\nZVRT_READ_OK\nZVRT_READ_BLOCKED\nZENOV_ZVRT_QEMU_OK\n'),
]
for old, new in replacements:
    count = text.count(old)
    assert count == 1, (old[:120], count)
    text = text.replace(old, new, 1)
readme.write_text(text)

index = Path('docs/INDEX.md')
text = index.read_text()
anchor = '- [`ON_ACCESS_PROTECTION_0.1.1.md`](ON_ACCESS_PROTECTION_0.1.1.md) — synchronous shell/ring-3 read mediation, READ-BLOCK/READ-AUDIT evidence, output scrubbing and internal verification exclusions.\n'
addition = anchor + '- [`VERIFIED_READS_0.1.1.md`](VERIFIED_READS_0.1.1.md) — signed ZVRT1 path-bound Merkle commitments, supervisor memory layout and valid/corrupt QEMU evidence.\n'
assert text.count(anchor) == 1, text.count(anchor)
index.write_text(text.replace(anchor, addition, 1))

model = Path('docs/SECURITY_MODEL_0.1.1.md')
text = model.read_text()
anchor = '- User-visible shell and ring-3 file reads are scanned synchronously after filesystem checksum verification; infected reads are persistently audited, scrubbed and denied, while suspicious reads require a durable `READ-AUDIT` before release.\n'
addition = anchor + '- Selected persistent files are additionally authenticated by independently signed ZVRT1 complete-file SHA-256 and path/size/chunk-bound Merkle roots before their bytes reach malware classification or executable appraisal. A mismatch is scrubbed, durably recorded as `READ-BLOCK` and returned as typed `checksum-mismatch`.\n'
assert text.count(anchor) == 1, text.count(anchor)
text = text.replace(anchor, addition, 1)
old = 'authenticated storage, dynamic-linker hardening'
new = 'transparent whole-volume or per-page authenticated storage, dynamic-linker hardening'
assert text.count(old) == 1, text.count(old)
text = text.replace(old, new, 1)
old = 'Controlled-folder paths, exact writer identities and mutation budgets are held in a fourth RSA-PSS-signed ZRWP1 domain.'
new = old + ' Selected persistent-file commitments are held in a fifth RSA-PSS-signed ZVRT1 domain.'
assert text.count(old) == 1, text.count(old)
text = text.replace(old, new, 1)
old = 'ZMID1 and ZRWP1 provide bounded write and on-access read prevention, not authenticated Merkle-tree storage, cloud antivirus, full EDR, archive analysis or machine-learning classification.'
new = 'ZMID1 and ZRWP1 provide bounded write and on-access read prevention. ZVRT1 provides bounded complete-file authenticated reads for a signed path set, not transparent whole-volume/per-page verity, cloud antivirus, full EDR, archive analysis or machine-learning classification.'
assert text.count(old) == 1, text.count(old)
model.write_text(text.replace(old, new, 1))
PY

cat > .github/workflows/zvrt.yml <<'EOF'
name: ZenovOS ZVRT1 Authenticated Reads

on:
  pull_request:
  merge_group:
  push:
    branches:
      - main

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

permissions:
  contents: read

jobs:
  authenticated-read-integrity:
    runs-on: ubuntu-24.04
    timeout-minutes: 180
    steps:
      - uses: actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd
        with:
          ref: ${{ github.event.pull_request.head.sha || github.sha }}
          fetch-depth: 0
          persist-credentials: false

      - name: Verify exact source revision
        shell: bash
        run: |
          set -euo pipefail
          test "$(git rev-parse HEAD)" = "${{ github.event.pull_request.head.sha || github.sha }}"
          test -z "$(git status --porcelain=v1)"

      - name: Install strict toolchain and QEMU
        run: sudo apt-get update && sudo apt-get install -y build-essential binutils openssl qemu-system-x86

      - name: Run strict host and freestanding checks
        run: make clean check

      - name: Run authenticated-read QEMU lifecycle
        run: make zvrt-qemu

      - name: Verify deterministic rebuild
        run: make deterministic

      - name: Verify source contracts
        shell: bash
        run: |
          set -euo pipefail
          grep -Fq 'namespace zvrt {' kernel/parts/zvrt_policy.inc
          grep -Fq 'FsStatus::checksum_mismatch' kernel/parts/security_io.inc
          grep -Fq 'ZVRT_WORKSPACE_OK address=0x00310000 bytes=4096 supervisor-only=yes' kernel/parts/zvrt_policy.inc
          grep -Fq 'constexpr uintptr_t pmm_bitmap = 0x00311000U;' kernel/parts/supervisor_layout.inc
          grep -Fq 'ASSERT(__kernel_end <= 0x000A0000' kernel/linker.ld
          grep -Fq '"zvrt_root_key_id": "d28215ec62269ffc"' build/build-manifest.json
          grep -Fq 'ZENOV_ZVRT_QEMU_OK valid=yes manifest_fail_closed=yes data_blocked=yes payload_disclosure=no multichunk=2 audit=durable' build/qemu/zvrt/summary.log
          echo 'ZVRT_SOURCE_CONTRACT_OK schema=1 pss=sha256-salt32 chunk=4096 records=4 leaves=5 typed-failure=yes'

      - name: Upload ZVRT1 evidence
        if: always()
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a
        with:
          name: zvrt-${{ github.run_id }}-${{ github.run_attempt }}
          if-no-files-found: error
          path: |
            build/KERNEL.BIN
            build/kernel.map
            build/zenov-os.img
            build/zenov-data.img
            build/build-manifest.json
            build/verified-reads-*.zvrt
            build/qemu/zvrt/
            security/zvrt-root-public.pem
            kernel/parts/zvrt_policy.inc
            kernel/parts/supervisor_layout.inc
            docs/VERIFIED_READS_0.1.1.md
EOF

git add \
  Makefile \
  README.md \
  docs/INDEX.md \
  docs/SECURITY_MODEL_0.1.1.md \
  docs/VERIFIED_READS_0.1.1.md \
  tests/qemu_zvrt.sh \
  .github/workflows/zvrt.yml

git diff --cached --check
bash -n tests/qemu_zvrt.sh

make clean check 2>&1 | tee "$log_dir/make-check.log"
make zvrt-qemu 2>&1 | tee "$log_dir/zvrt-qemu.log"
make deterministic 2>&1 | tee "$log_dir/deterministic.log"

grep -Fq 'ZENOV_ZVRT_QEMU_OK valid=yes manifest_fail_closed=yes data_blocked=yes payload_disclosure=no multichunk=2 audit=durable' "$log_dir/zvrt-qemu.log"
grep -Fq 'ZENOV_ZVRT_IMAGE_OK version=1 records=4 leaves=5 multichunk=1 key=d28215ec62269ffc' "$log_dir/zvrt-qemu.log"
grep -Fq 'zenovfs-audit-verify: OK' "$log_dir/zvrt-qemu.log"
grep -Fq 'deterministic rebuild: OK' "$log_dir/deterministic.log"

cat > /tmp/zvrt-final-paths <<'EOF'
.github/workflows/zvrt.yml
Makefile
README.md
docs/INDEX.md
docs/SECURITY_MODEL_0.1.1.md
docs/VERIFIED_READS_0.1.1.md
kernel/kernel.cpp
kernel/parts/memory.inc
kernel/parts/security_guard.inc
kernel/parts/security_io.inc
kernel/parts/security_paths.inc
kernel/parts/supervisor_layout.inc
kernel/parts/zvrt_policy.inc
security/zvrt-root-public.pem
security/zvrt_crypto_material.hpp
tests/qemu_zvrt.sh
tools/zenovfs_builder.cpp
tools/zenovfs_zvrt_corrupt.cpp
tools/zenovfs_zvrt_verify.cpp
tools/zvrt_builder.cpp
tools/zvrt_verify.cpp
EOF
sort -o /tmp/zvrt-final-paths /tmp/zvrt-final-paths

{
  git diff --name-only "$TARGET_BASE" HEAD
  git diff --cached --name-only
} | sort -u | grep -v '^\.github/agent/' | grep -v '^\.github/workflows/zvrt-qemu-verify.yml$' > /tmp/zvrt-actual-paths

diff -u /tmp/zvrt-final-paths /tmp/zvrt-actual-paths | tee "$log_dir/path-contract.log"

rm -f /tmp/zvrt-final.index
GIT_INDEX_FILE=/tmp/zvrt-final.index git read-tree "${TARGET_BASE}^{tree}"
while IFS= read -r path; do
  GIT_INDEX_FILE=/tmp/zvrt-final.index git add -- "$path"
done < /tmp/zvrt-final-paths
final_tree="$(GIT_INDEX_FILE=/tmp/zvrt-final.index git write-tree)"
printf '%s\n' "$final_tree" | tee "$log_dir/verified-final-tree.log"

tar --format=posix --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
  -cf /tmp/zvrt-final.tar -T /tmp/zvrt-final-paths
tar_sha="$(sha256sum /tmp/zvrt-final.tar | awk '{print $1}')"

{
  printf 'target_base=%s\n' "$TARGET_BASE"
  printf 'final_tree=%s\n' "$final_tree"
  printf 'tar_sha256=%s\n' "$tar_sha"
  while IFS= read -r path; do
    mode="$(git ls-files -s -- "$path" | awk '{print $1}')"
    blob="$(git hash-object "$path")"
    printf 'path=%s\tmode=%s\tblob=%s\n' "$path" "$mode" "$blob"
  done < /tmp/zvrt-final-paths
} > /tmp/zvrt-final-manifest.txt
cat /tmp/zvrt-final-manifest.txt | tee "$log_dir/final-manifest.log"

printf 'ZVRT_FINAL_GATE_OK base=%s tree=%s files=21 tar=%s\n' \
  "$TARGET_BASE" "$final_tree" "$tar_sha" | tee "$log_dir/final-marker.log"
