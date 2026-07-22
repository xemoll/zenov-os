#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/block-status}"
PROMPT='zenov> '

mkdir -p "$OUT"
rm -f "$OUT"/serial.log "$OUT"/monitor.log "$OUT"/qemu.stderr "$OUT"/runtime.img "$OUT"/summary.log

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-900}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-block-status: missing serial marker: $text" >&2
  return 1
}

send_text() {
  local text="$1" char lower i
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="${char,,}"; echo "sendkey shift-$lower 10" ;;
      ' ') echo 'sendkey spc 10' ;;
      '-') echo 'sendkey minus 10' ;;
      *) echo "qemu-block-status: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

controller() {
  local serial="$1"
  wait_for_serial "$serial" 'ATA_DEADLINE_DRIVER_READY clock=pit100 timeout=3000ms reset=5000ms attempts=2' || { echo quit; return 1; }
  wait_for_serial "$serial" 'BLOCK_DEVICE_TYPED_ABI_READY version=1' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  send_text 'disk status'; echo 'sendkey ret 10'
  wait_for_serial "$serial" 'BLOCK_DEVICE_COUNTERS_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'BLOCK_DEVICE_STATUS_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'BLOCK_RESULT_API_OK version=2' || { echo quit; return 1; }
  wait_for_serial "$serial" 'BLOCK_DEVICE_TYPED_ABI_OK version=1' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" ]] || { echo 'qemu-block-status: boot and data images are required' >&2; exit 2; }
cp "$DATA_IMAGE" "$OUT/runtime.img"
SERIAL_ABS="$(cd "$OUT" && pwd)/serial.log"
RUNTIME_ABS="$(cd "$OUT" && pwd)/runtime.img"

set +e
controller "$SERIAL_ABS" | timeout 120s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_ABS,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL_ABS" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e

if [[ $status -ne 0 ]]; then
  echo "qemu-block-status: QEMU/controller failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$OUT/serial.log" >&2 || true
  exit 1
fi

[[ ! -s "$OUT/qemu.stderr" ]] || { echo 'qemu-block-status: non-empty QEMU stderr' >&2; cat "$OUT/qemu.stderr" >&2; exit 1; }
grep -Fq 'BLOCK_DEVICE_TYPED_ABI_READY version=1' "$OUT/serial.log"
grep -Fq 'BLOCK_DEVICE_COUNTERS_OK' "$OUT/serial.log"
grep -Fq 'BLOCK_DEVICE_STATUS_OK' "$OUT/serial.log"
grep -Fq 'BLOCK_RESULT_API_OK version=2' "$OUT/serial.log"
grep -Fq 'BLOCK_DEVICE_TYPED_ABI_OK version=1' "$OUT/serial.log"
! grep -Eq 'BLOCK_DEVICE_COUNTERS_INVALID|KERNEL PANIC|DOUBLE FAULT|ASSERT|PS2_MOUSE_UNAVAILABLE' "$OUT/serial.log"
printf 'BLOCK_STATUS_QEMU_OK command="disk status" typed-results=2 typed-abi=1 counters=consistent\n' | tee "$OUT/summary.log"
