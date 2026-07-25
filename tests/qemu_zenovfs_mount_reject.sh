#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
CORRUPTOR="${3:-build/zenovfs-mount-corrupt}"
OUT="${4:-build/qemu/zenovfs-mount-reject}"

mkdir -p "$OUT"
[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" && -x "$CORRUPTOR" ]] || {
  echo 'qemu-zenovfs-mount-reject: boot image, data image and corruptor are required' >&2
  exit 2
}

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-450}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-zenovfs-mount-reject: missing serial marker: $text" >&2
  return 1
}

run_case() {
  local mode="$1" reason="$2"
  local image="$OUT/$mode.img" serial="$OUT/$mode.serial.log"
  local monitor="$OUT/$mode.monitor.log" stderr="$OUT/$mode.qemu.stderr"
  rm -f "$image" "$serial" "$monitor" "$stderr"
  "$CORRUPTOR" "$DATA_IMAGE" "$mode" "$image"

  controller() {
    wait_for_serial "$serial" "ZENOVFS_MOUNT_REJECTED reason=$reason" || { echo quit; return 1; }
    wait_for_serial "$serial" 'Storage: ZenovFS mount failed' || { echo quit; return 1; }
    wait_for_serial "$serial" 'KERNEL PANIC' || { echo quit; return 1; }
    echo quit
  }

  set +e
  controller | timeout 60s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$image,format=raw,if=ide" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-zenovfs-mount-reject: $mode failed with status $status" >&2
    cat "$monitor" "$stderr" "$serial" >&2 || true
    return 1
  fi
  [[ "$(grep -Fc "ZENOVFS_MOUNT_REJECTED reason=$reason" "$serial")" -eq 1 ]]
  grep -Fq 'KERNEL PANIC' "$serial"
  ! grep -Fq 'ZENOVOS_UI_READY' "$serial"
  printf 'ZENOVFS_MALFORMED_BOOT_REJECTED mode=%s reason=%s\n' "$mode" "$reason"
}

run_case entry-sectors entry-sectors
run_case metadata-layout metadata-layout
run_case slot-size slot-size
run_case entry-path entry-path
run_case component-depth entry-path
run_case duplicate-path duplicate-path
run_case transition-old-mismatch transition-old-mismatch

printf 'ZENOVFS_MOUNT_QEMU_REJECTION_OK cases=7 ui_reached=0\n' | tee "$OUT/summary.log"
