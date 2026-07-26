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

python3 - <<'PY'
from pathlib import Path

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
constexpr uintptr_t record_workspace_address = security_guard::write_scan_workspace_address + security_guard::write_scan_workspace_bytes;
constexpr uint32_t record_workspace_bytes = 2U * max_records * sizeof(Record);
static_assert(record_workspace_address == 0x00310000U);
static_assert(record_workspace_bytes == 4096U);
static_assert(record_workspace_address >= security_guard::write_scan_workspace_address + security_guard::write_scan_workspace_bytes);
static_assert(record_workspace_address + record_workspace_bytes <= process::user_base);
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

git add kernel/parts/security_io.inc kernel/parts/zvrt_policy.inc
git diff --cached --check
git diff --cached --stat | tee "$log_dir/runtime-diff-stat.log"

make clean check 2>&1 | tee "$log_dir/make-check.log"
grep -Fq 'ZENOV_ZVRT_IMAGE_OK version=1 records=4 leaves=5 multichunk=1 key=d28215ec62269ffc' \
  "$log_dir/make-check.log"
grep -Fq 'zvrt-verify: OK version=1 records=4 chunk=4096 leaves=5 multichunk=1 key=d28215ec62269ffc' \
  "$log_dir/make-check.log"
test -s build/qemu/zenov-data-zvrt-manifest-corrupt.img
test -s build/qemu/zenov-data-zvrt-data-corrupt.img

cat > /tmp/zvrt-expected-paths <<'EOF'
Makefile
kernel/kernel.cpp
kernel/parts/security_io.inc
kernel/parts/security_paths.inc
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

printf 'ZVRT_COMPILE_GATE_OK base=%s tree=%s files=12 tar=%s\n' \
  "$TARGET_BASE" "$production_tree" "$tar_sha" | tee "$log_dir/final-marker.log"
