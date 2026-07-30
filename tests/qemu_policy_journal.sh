#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
HOT_IMAGE="${2:-build/zenov-data-policy-journal-hot.img}"
CORRUPT_IMAGE="${3:-build/zenov-data-policy-journal-corrupt.img}"
OUT="${4:-build/qemu/policy-journal}"
PROMPT='zenov> '

mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.stderr "$OUT"/*.img

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-1200}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-policy-journal: missing serial marker: $text" >&2
  return 1
}

controller_hot() {
  local serial="$1"
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'POLICY_TRANSACTION_RECOVERY_OK domain=ZMID' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOV_GUARD_AUDIT_REPLAY_OK count=0 next=1' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOV_GUARD_AUDIT_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZMID_DATABASE_VERSION_OK version=1' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZMID_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'POLICY_TRANSACTION_JOURNAL_PROTECTED_PATH_TEST_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  sleep 0.3
  echo quit
}

controller_clean_reboot() {
  local serial="$1"
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'POLICY_TRANSACTION_JOURNAL_CLEAN' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOV_GUARD_AUDIT_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZMID_DATABASE_VERSION_OK version=1' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  sleep 0.3
  echo quit
}

controller_corrupt() {
  local serial="$1"
  wait_for_serial "$serial" 'ZENOVOS_BOOT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'Persistent signed policy transaction recovery failed.' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

run_phase() {
  local name="$1" image="$2" controller="$3"
  local serial="$OUT/serial-$name.log" monitor="$OUT/monitor-$name.log" stderr="$OUT/qemu-$name.stderr"
  local image_abs serial_abs status
  image_abs="$(cd "$(dirname "$image")" && pwd)/$(basename "$image")"
  serial_abs="$(cd "$OUT" && pwd)/serial-$name.log"
  set +e
  "$controller" "$serial_abs" | timeout 180s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$image_abs,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial_abs" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-policy-journal: phase $name failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    exit 1
  fi
  [[ ! -s "$stderr" ]] || {
    echo "qemu-policy-journal: phase $name produced QEMU stderr" >&2
    cat "$stderr" >&2
    exit 1
  }
}

[[ -f "$BOOT_IMAGE" && -f "$HOT_IMAGE" && -f "$CORRUPT_IMAGE" ]] || {
  echo 'qemu-policy-journal: boot, hot and corrupt images are required' >&2
  exit 2
}

cp "$HOT_IMAGE" "$OUT/hot-runtime.img"
cmp "$HOT_IMAGE" "$OUT/hot-runtime.img"
sync -f "$OUT/hot-runtime.img"
run_phase hot "$OUT/hot-runtime.img" controller_hot
run_phase clean-reboot "$OUT/hot-runtime.img" controller_clean_reboot

cp "$CORRUPT_IMAGE" "$OUT/corrupt-runtime.img"
cmp "$CORRUPT_IMAGE" "$OUT/corrupt-runtime.img"
sync -f "$OUT/corrupt-runtime.img"
run_phase corrupt "$OUT/corrupt-runtime.img" controller_corrupt

grep -Fq 'POLICY_TRANSACTION_RECOVERY_OK domain=ZMID' "$OUT/serial-hot.log"
grep -Fq 'ZENOV_GUARD_AUDIT_REPLAY_OK count=0 next=1' "$OUT/serial-hot.log"
grep -Fq 'ZMID_DATABASE_VERSION_OK version=1' "$OUT/serial-hot.log"
grep -Fq 'POLICY_TRANSACTION_JOURNAL_CLEAN' "$OUT/serial-clean-reboot.log"
! grep -Fq 'POLICY_TRANSACTION_RECOVERY_OK' "$OUT/serial-clean-reboot.log"
grep -Fq 'Persistent signed policy transaction recovery failed.' "$OUT/serial-corrupt.log"
! grep -Fq 'ZENOV_GUARD_AUDIT_READY' "$OUT/serial-corrupt.log"
! grep -Fq 'ZGDB_READY' "$OUT/serial-corrupt.log"
! grep -Fq 'ZENOVOS_UI_READY' "$OUT/serial-corrupt.log"
! grep -Eq 'DOUBLE FAULT|ASSERT' "$OUT/serial-hot.log"
! grep -Eq 'DOUBLE FAULT|ASSERT' "$OUT/serial-clean-reboot.log"

printf 'POLICY_TRANSACTION_QEMU_OK hot=replayed domain=ZMID policy=1 version=1 audit=restored journal=cleared reboot=clean corrupt=fail-closed\n' \
  | tee "$OUT/summary.log"
