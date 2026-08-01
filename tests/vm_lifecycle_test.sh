#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SEED="${1:-$ROOT/build/zenov-data.img}"
SCRIPT="$ROOT/packaging/manage-vm.sh"
WORK="${2:-$ROOT/build/vm-lifecycle-test}"
DIST="$WORK/dist"
STATE="$WORK/state"
BACKUPS="$WORK/backups"

for tool in qemu-img sha256sum cmp python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "vm-lifecycle-test: missing $tool" >&2; exit 1; }
done
[[ -s "$SEED" ]] || { echo "vm-lifecycle-test: missing seed: $SEED" >&2; exit 1; }

rm -rf -- "$WORK"
mkdir -p -- "$DIST"
install -m 0644 "$SEED" "$DIST/ZenovOS-0.1.1-data.img"
(
  cd "$DIST"
  sha256sum ZenovOS-0.1.1-data.img > SHA256SUMS.txt
)
install -m 0755 "$SCRIPT" "$DIST/manage-vm.sh"

run_manager() {
  ZENOV_VM_FORMAT=qcow2 \
  ZENOV_VM_STATE_DIR="$STATE" \
  ZENOV_VM_BACKUP_DIR="$BACKUPS" \
    "$DIST/manage-vm.sh" "$@"
}

run_format_manager() {
  local format="$1"
  shift
  ZENOV_VM_FORMAT="$format" \
  ZENOV_VM_STATE_DIR="$WORK/state-$format" \
  ZENOV_VM_BACKUP_DIR="$WORK/backups-$format" \
    "$DIST/manage-vm.sh" "$@"
}

run_manager create | grep -Fq 'created='
run_manager verify | grep -Fq 'verified='
TARGET="$STATE/ZenovOS-0.1.1-data.qcow2"
[[ -s "$TARGET" ]]
qemu-img check -q -f qcow2 "$TARGET"

# Mutate one guest-visible byte through a raw roundtrip, then ensure backup,
# reset and restore preserve exactly the expected payload.
qemu-img convert -q -f qcow2 -O raw "$TARGET" "$WORK/mutated.raw"
printf '\x5a' | dd of="$WORK/mutated.raw" bs=1 seek=1048576 conv=notrunc status=none
qemu-img convert -q -f raw -O qcow2 -o compat=1.1,cluster_size=65536 "$WORK/mutated.raw" "$TARGET.new"
mv -f "$TARGET.new" "$TARGET"

BACKUP_LINE="$(run_manager backup)"
BACKUP="${BACKUP_LINE#backup=}"
[[ -s "$BACKUP" && -s "$BACKUP.sha256" ]]
(cd "$(dirname "$BACKUP")" && sha256sum -c "$(basename "$BACKUP").sha256")

run_manager reset | grep -Fq 'reset='
qemu-img convert -q -f qcow2 -O raw "$TARGET" "$WORK/reset.raw"
cmp "$SEED" "$WORK/reset.raw"

run_manager restore "$BACKUP" | grep -Fq 'restored='
qemu-img convert -q -f qcow2 -O raw "$TARGET" "$WORK/restored.raw"
cmp "$WORK/mutated.raw" "$WORK/restored.raw"

# A checksum mismatch must fail before the runtime disk changes.
TAMPERED="$WORK/tampered.qcow2"
cp "$BACKUP" "$TAMPERED"
TAMPERED_HASH="$(sha256sum "$TAMPERED" | cut -d' ' -f1)"
printf '%s  %s\n' "$TAMPERED_HASH" "$(basename "$TAMPERED")" > "$TAMPERED.sha256"
printf '\x01' | dd of="$TAMPERED" bs=1 seek=4096 conv=notrunc status=none
BEFORE="$(sha256sum "$TARGET" | cut -d' ' -f1)"
if run_manager restore "$TAMPERED" >"$WORK/tamper.stdout" 2>"$WORK/tamper.stderr"; then
  echo "vm-lifecycle-test: tampered backup unexpectedly restored" >&2
  exit 1
fi
grep -Fq 'backup checksum mismatch' "$WORK/tamper.stderr"
AFTER="$(sha256sum "$TARGET" | cut -d' ' -f1)"
[[ "$BEFORE" == "$AFTER" ]]

# A concurrent/stale lock must fail closed without touching the disk.
mkdir "$STATE/.zenov-vm.lock"
BEFORE="$(sha256sum "$TARGET" | cut -d' ' -f1)"
if run_manager reset >"$WORK/lock.stdout" 2>"$WORK/lock.stderr"; then
  echo "vm-lifecycle-test: stale lock unexpectedly accepted" >&2
  exit 1
fi
grep -Fq 'another lifecycle operation is active' "$WORK/lock.stderr"
AFTER="$(sha256sum "$TARGET" | cut -d' ' -f1)"
[[ "$BEFORE" == "$AFTER" ]]
rmdir "$STATE/.zenov-vm.lock"

run_manager remove | grep -Fq 'removed='
[[ ! -e "$TARGET" ]]
find "$BACKUPS" -maxdepth 1 -type f -name '*.qcow2' | grep -q .

# Exercise every advertised runtime format through the lifecycle manager.
for format in raw vdi vmdk; do
  run_format_manager "$format" create | grep -Fq 'created='
  run_format_manager "$format" verify | grep -Fq 'verified='
  run_format_manager "$format" remove | grep -Fq 'removed='
  find "$WORK/backups-$format" -maxdepth 1 -type f -name '*.*' | grep -q .
done

printf 'VM_LIFECYCLE_OK create=atomic verify=qemu-img backup=checksummed reset=seed restore=exact tamper=fail-closed lock=fail-closed remove=backup-first formats=raw,qcow2,vdi,vmdk\n'
