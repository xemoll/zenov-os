#!/usr/bin/env bash
set -euo pipefail

: "${TARGET_BASE:?TARGET_BASE is required}"
: "${PATCH_RUN_ID:?PATCH_RUN_ID is required}"
: "${PATCH_ARTIFACT:?PATCH_ARTIFACT is required}"
: "${GH_TOKEN:?GH_TOKEN is required}"
: "${GITHUB_API_URL:?GITHUB_API_URL is required}"
: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"

readonly patch_file='/tmp/zvrt-integration.patch'
readonly patch_sha='e5bdc34cd91925457574fe0f7a48d13aa089143d877828caec4ae4ebab66ad37'
readonly log_dir='ci-logs/zvrt-verification'
mkdir -p "$log_dir" /tmp/zvrt-evidence

curl --fail-with-body -sS \
  -H "Authorization: Bearer ${GH_TOKEN}" \
  -H 'Accept: application/vnd.github+json' \
  -H 'X-GitHub-Api-Version: 2022-11-28' \
  "${GITHUB_API_URL}/repos/${GITHUB_REPOSITORY}/actions/runs/${PATCH_RUN_ID}/artifacts?per_page=100" \
  > /tmp/zvrt-artifacts.json

artifact_id="$(python3 - <<'PY'
import json
import os

with open('/tmp/zvrt-artifacts.json', encoding='utf-8') as source:
    data = json.load(source)
matches = [
    artifact for artifact in data.get('artifacts', [])
    if artifact.get('name') == os.environ['PATCH_ARTIFACT'] and not artifact.get('expired')
]
assert len(matches) == 1, [(artifact.get('id'), artifact.get('name')) for artifact in data.get('artifacts', [])]
print(matches[0]['id'])
PY
)"

curl --fail-with-body -L -sS \
  -H "Authorization: Bearer ${GH_TOKEN}" \
  -H 'Accept: application/vnd.github+json' \
  -H 'X-GitHub-Api-Version: 2022-11-28' \
  "${GITHUB_API_URL}/repos/${GITHUB_REPOSITORY}/actions/artifacts/${artifact_id}/zip" \
  -o /tmp/zvrt-evidence.zip
unzip -q /tmp/zvrt-evidence.zip -d /tmp/zvrt-evidence
source_patch="$(find /tmp/zvrt-evidence -type f -name zvrt-integration.patch -print -quit)"
test -n "$source_patch"
cp "$source_patch" "$patch_file"

printf '%s  %s\n' "$patch_sha" "$patch_file" | sha256sum -c - \
  | tee "$log_dir/patch-sha256.log"
test "$(stat -c '%s' "$patch_file")" -eq 21841

git apply --index --check "$patch_file"
git apply --index "$patch_file"

cat > kernel/parts/supervisor_layout.inc <<'EOF'
namespace supervisor_layout {
constexpr uintptr_t heap_start = 0x00100000U;
constexpr uintptr_t heap_end = 0x00300000U;
constexpr uintptr_t scan_workspace = 0x00300000U;
constexpr uint32_t scan_workspace_bytes = 0x00010000U;
constexpr uintptr_t zvrt_records = 0x00310000U;
constexpr uint32_t zvrt_records_bytes = 0x00001000U;
constexpr uintptr_t pmm_bitmap = 0x00311000U;
constexpr uint32_t pmm_bitmap_bytes = 0x00001000U;
constexpr uintptr_t user_base = 0x00400000U;
constexpr uintptr_t pmm_reserved_end = 0x00800000U;

static_assert(heap_start < heap_end);
static_assert(heap_end == scan_workspace);
static_assert(scan_workspace + scan_workspace_bytes == zvrt_records);
static_assert(zvrt_records + zvrt_records_bytes == pmm_bitmap);
static_assert(pmm_bitmap + pmm_bitmap_bytes <= user_base);
static_assert(user_base < pmm_reserved_end);
} // namespace supervisor_layout
EOF

python3 - <<'PY'
from pathlib import Path

kernel = Path('kernel/kernel.cpp')
text = kernel.read_text()
old = '#include "parts/hardware_irq_staging.inc"\n#include "parts/memory.inc"\n'
new = '#include "parts/hardware_irq_staging.inc"\n#include "parts/supervisor_layout.inc"\n#include "parts/memory.inc"\n'
assert text.count(old) == 1, text.count(old)
kernel.write_text(text.replace(old, new, 1))

memory = Path('kernel/parts/memory.inc')
text = memory.read_text()
old = '''constexpr uintptr_t reserved_end = 0x00800000U;
constexpr uint32_t self_test_frames = 16U;
uint32_t frame_bitmap[bitmap_words];
'''
new = '''constexpr uintptr_t reserved_end = supervisor_layout::pmm_reserved_end;
constexpr uint32_t self_test_frames = 16U;
constexpr uint32_t frame_bitmap_bytes = bitmap_words * sizeof(uint32_t);
static_assert(frame_bitmap_bytes == supervisor_layout::pmm_bitmap_bytes);
static_assert(supervisor_layout::pmm_bitmap + frame_bitmap_bytes <= supervisor_layout::user_base);
uint32_t* frame_bitmap = reinterpret_cast<uint32_t*>(supervisor_layout::pmm_bitmap);
'''
assert text.count(old) == 1, text.count(old)
text = text.replace(old, new, 1)
old = '''constexpr uintptr_t start_address = 0x00100000U;
constexpr uintptr_t limit_address = 0x00300000U;
'''
new = '''constexpr uintptr_t start_address = supervisor_layout::heap_start;
constexpr uintptr_t limit_address = supervisor_layout::heap_end;
'''
assert text.count(old) == 1, text.count(old)
memory.write_text(text.replace(old, new, 1))

security_guard = Path('kernel/parts/security_guard.inc')
text = security_guard.read_text()
old = '''constexpr uintptr_t write_scan_workspace_address = 0x00300000U;
constexpr uint32_t write_scan_workspace_bytes = process::application_buffer_bytes;
static_assert(write_scan_workspace_address >= heap::limit_address);
static_assert(write_scan_workspace_address + write_scan_workspace_bytes <= process::user_base);
'''
new = '''constexpr uintptr_t write_scan_workspace_address = supervisor_layout::scan_workspace;
constexpr uint32_t write_scan_workspace_bytes = supervisor_layout::scan_workspace_bytes;
static_assert(write_scan_workspace_bytes == process::application_buffer_bytes);
static_assert(write_scan_workspace_address >= heap::limit_address);
static_assert(write_scan_workspace_address + write_scan_workspace_bytes <= process::user_base);
static_assert(process::user_base == supervisor_layout::user_base);
'''
assert text.count(old) == 1, text.count(old)
security_guard.write_text(text.replace(old, new, 1))

security_io = Path('kernel/parts/security_io.inc')
text = security_io.read_text()
old = '''void guarded_cat(const char* input) {
    uint32_t size = 0U;
    const FsResult result = fs_record_result(guarded_read_file_result(input, file_buffer, max_file_bytes(), size));
    if (!result.ok()) {
        console::write("Read failed: "); console::line(fs_status_name(result.status));
        return;
    }
'''
new = '''void guarded_cat(const char* input) {
    uint32_t size = 0U;
    if (!guarded_read_file(input, file_buffer, max_file_bytes(), size)) {
        console::warning("File unavailable, corrupt or blocked by ZenovGuard.");
        return;
    }
'''
assert text.count(old) == 1, text.count(old)
security_io.write_text(text.replace(old, new, 1))

policy = Path('kernel/parts/zvrt_policy.inc')
text = policy.read_text()
old = '''enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, engine, key, records };
Record active_records[max_records]{};
Record candidate_records[max_records]{};
uint32_t active_record_count = 0U, active_manifest_version = 0U, persistent_manifest_version = 0U;
'''
new = '''enum class Validation : uint8_t { okay, malformed, digest, signature, rollback, engine, key, records };
constexpr uintptr_t record_workspace_address = supervisor_layout::zvrt_records;
constexpr uint32_t record_workspace_bytes = 2U * max_records * sizeof(Record);
static_assert(record_workspace_bytes == supervisor_layout::zvrt_records_bytes);
static_assert(record_workspace_address + record_workspace_bytes <= supervisor_layout::pmm_bitmap);
Record* active_records = reinterpret_cast<Record*>(record_workspace_address);
Record* candidate_records = active_records + max_records;
uint32_t active_record_count = 0U, active_manifest_version = 0U, persistent_manifest_version = 0U;
'''
assert text.count(old) == 1, text.count(old)
text = text.replace(old, new, 1)
old = '    memset(active_records, 0, sizeof(active_records));\n'
new = '    memset(active_records, 0, max_records * sizeof(Record));\n'
assert text.count(old) == 1, text.count(old)
text = text.replace(old, new, 1)
old = '    serial::line("ZVRT_ROOT_KEY_OK id=d28215ec62269ffc");\n'
new = '''    serial::line("ZVRT_ROOT_KEY_OK id=d28215ec62269ffc");
    serial::line("ZVRT_WORKSPACE_OK address=0x00310000 bytes=4096 supervisor-only=yes");
'''
assert text.count(old) == 1, text.count(old)
policy.write_text(text.replace(old, new, 1))
PY

git add \
  kernel/kernel.cpp \
  kernel/parts/memory.inc \
  kernel/parts/security_guard.inc \
  kernel/parts/security_io.inc \
  kernel/parts/supervisor_layout.inc \
  kernel/parts/zvrt_policy.inc

git diff --cached --check
git diff --cached --stat | tee "$log_dir/runtime-diff-stat.log"

if ! make clean check 2>&1 | tee "$log_dir/make-check.log"; then
  if grep -Fq 'kernel low-memory image overlaps VGA aperture' "$log_dir/make-check.log" && \
     test -s build/entry.o && test -s build/interrupts.o && test -s build/user-runtime.o && test -s build/kernel.o; then
    cp kernel/linker.ld /tmp/zvrt-linker-diagnostic.ld
    sed -i '/kernel low-memory image overlaps VGA aperture/d' /tmp/zvrt-linker-diagnostic.ld
    ld -m elf_i386 -T /tmp/zvrt-linker-diagnostic.ld \
      -Map "$log_dir/kernel-diagnostic.map" \
      -o /tmp/zvrt-kernel-diagnostic.elf \
      build/entry.o build/interrupts.o build/user-runtime.o build/kernel.o
    nm -n /tmp/zvrt-kernel-diagnostic.elf > "$log_dir/kernel-diagnostic.nm"
    grep -E '__kernel_end|__bss_start|__bss_end' "$log_dir/kernel-diagnostic.map" \
      > "$log_dir/kernel-diagnostic-boundaries.log" || true
  fi
  exit 1
fi

grep -Fq 'ZENOV_ZVRT_IMAGE_OK version=1 records=4 leaves=5 multichunk=1 key=d28215ec62269ffc' \
  "$log_dir/make-check.log"
grep -Fq 'zvrt-verify: OK version=1 records=4 chunk=4096 leaves=5 multichunk=1 key=d28215ec62269ffc' \
  "$log_dir/make-check.log"
test -s build/qemu/zenov-data-zvrt-manifest-corrupt.img
test -s build/qemu/zenov-data-zvrt-data-corrupt.img

grep -F '__kernel_end' build/kernel.map | tail -1 | tee "$log_dir/kernel-end.log"

cat > /tmp/zvrt-expected-paths <<'EOF'
Makefile
kernel/kernel.cpp
kernel/parts/memory.inc
kernel/parts/security_guard.inc
kernel/parts/security_io.inc
kernel/parts/security_paths.inc
kernel/parts/supervisor_layout.inc
kernel/parts/zvrt_policy.inc
security/zvrt-root-public.pem
security/zvrt_crypto_material.hpp
tools/zenovfs_builder.cpp
tools/zenovfs_zvrt_corrupt.cpp
tools/zenovfs_zvrt_verify.cpp
tools/zvrt_builder.cpp
tools/zvrt_verify.cpp
EOF
sort -o /tmp/zvrt-expected-paths /tmp/zvrt-expected-paths

{
  git diff --name-only "$TARGET_BASE" HEAD
  git diff --cached --name-only
} | sort -u | grep -v '^\.github/' > /tmp/zvrt-production-paths

diff -u /tmp/zvrt-expected-paths /tmp/zvrt-production-paths \
  | tee "$log_dir/path-contract.log"

rm -f /tmp/zvrt-production.index
GIT_INDEX_FILE=/tmp/zvrt-production.index git read-tree "${TARGET_BASE}^{tree}"
while IFS= read -r path; do
  GIT_INDEX_FILE=/tmp/zvrt-production.index git add -- "$path"
done < /tmp/zvrt-production-paths
production_tree="$(GIT_INDEX_FILE=/tmp/zvrt-production.index git write-tree)"
printf '%s\n' "$production_tree" | tee "$log_dir/verified-production-tree.log"

tar --format=posix --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
  -cf /tmp/zvrt-production.tar -T /tmp/zvrt-production-paths
tar_sha="$(sha256sum /tmp/zvrt-production.tar | awk '{print $1}')"

{
  printf 'target_base=%s\n' "$TARGET_BASE"
  printf 'production_tree=%s\n' "$production_tree"
  printf 'tar_sha256=%s\n' "$tar_sha"
  printf 'patch_sha256=%s\n' "$patch_sha"
  while IFS= read -r path; do
    mode="$(git ls-files -s -- "$path" | awk '{print $1}')"
    blob="$(git hash-object "$path")"
    printf 'path=%s\tmode=%s\tblob=%s\n' "$path" "$mode" "$blob"
  done < /tmp/zvrt-production-paths
} > /tmp/zvrt-production-manifest.txt
cat /tmp/zvrt-production-manifest.txt | tee "$log_dir/production-manifest.log"

printf 'ZVRT_COMPILE_GATE_OK base=%s tree=%s files=15 tar=%s\n' \
  "$TARGET_BASE" "$production_tree" "$tar_sha" | tee "$log_dir/final-marker.log"
