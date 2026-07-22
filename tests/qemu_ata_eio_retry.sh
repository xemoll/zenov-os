#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
BLKDEBUG_CONFIG="${3:-tests/blkdebug/ata-write-eio-once.conf}"
OUT="${4:-build/qemu/ata-eio-retry}"
PROMPT='zenov> '
PROOF_PATH='/data/ata-retry-proof.txt'
PROOF_TEXT='ATA_RETRY_PROOF_OK'

mkdir -p "$OUT"
rm -f "$OUT"/serial.log "$OUT"/monitor.log "$OUT"/qemu.stderr \
  "$OUT"/runtime.img "$OUT"/summary.log

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-900}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-ata-eio: missing serial marker: $text" >&2
  return 1
}

send_text() {
  local text="$1" char lower
  local i
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="${char,,}"; echo "sendkey shift-$lower 10" ;;
      ' ') echo 'sendkey spc 10' ;;
      '.') echo 'sendkey dot 10' ;;
      '-') echo 'sendkey minus 10' ;;
      '_') echo 'sendkey shift-minus 10' ;;
      '/') echo 'sendkey slash 10' ;;
      *) echo "qemu-ata-eio: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() {
  send_text "$1"
  echo 'sendkey ret 10'
}

controller() {
  local serial="$1"
  wait_for_serial "$serial" 'MONOTONIC_TICK_READY hz=100 irq-mask=timer-only' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ATA_DEADLINE_DRIVER_READY clock=pit100 timeout=3000ms reset=5000ms attempts=2' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOV_GUARD_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENPKG_MANAGER_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'INPUT_IRQS_READY keyboard=1 mouse=12' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }

  send_command "write $PROOF_PATH $PROOF_TEXT"
  wait_for_serial "$serial" 'ATA_RECOVERY_REVALIDATE_OK command=identify capacity=stable' || { echo quit; return 1; }
  wait_for_serial "$serial" 'WRITE_OK' || { echo quit; return 1; }
  send_command "cat $PROOF_PATH"
  wait_for_serial "$serial" "$PROOF_TEXT" || { echo quit; return 1; }
  send_command 'fsck'
  wait_for_serial "$serial" 'ZENOVFS_FSCK_OK' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" && -f "$BLKDEBUG_CONFIG" ]] || {
  echo 'qemu-ata-eio: boot image, data image and blkdebug config are required' >&2
  exit 2
}

cp "$DATA_IMAGE" "$OUT/runtime.img"
cmp "$DATA_IMAGE" "$OUT/runtime.img"
sync -f "$OUT/runtime.img"
cmp "$DATA_IMAGE" "$OUT/runtime.img"

CONFIG_ABS="$(cd "$(dirname "$BLKDEBUG_CONFIG")" && pwd)/$(basename "$BLKDEBUG_CONFIG")"
RUNTIME_ABS="$(cd "$OUT" && pwd)/runtime.img"
SERIAL_ABS="$(cd "$OUT" && pwd)/serial.log"

set +e
controller "$SERIAL_ABS" | timeout 120s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -blockdev "driver=file,node-name=runtime-file,filename=$RUNTIME_ABS,cache.direct=off,cache.no-flush=off" \
  -blockdev 'driver=raw,node-name=runtime-raw,file=runtime-file' \
  -blockdev "driver=blkdebug,node-name=runtime-debug,config=$CONFIG_ABS,image=runtime-raw" \
  -device 'ide-hd,id=ata-eio-disk,drive=runtime-debug,bus=ide.0,unit=0' \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL_ABS" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e

if [[ $status -ne 0 ]]; then
  echo "qemu-ata-eio: QEMU/controller failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$OUT/serial.log" >&2 || true
  exit 1
fi

[[ ! -s "$OUT/qemu.stderr" ]] || {
  echo 'qemu-ata-eio: non-empty QEMU stderr' >&2
  cat "$OUT/qemu.stderr" >&2
  exit 1
}

[[ "$(grep -Fc 'ATA_IO_ERROR op=write reason=' "$OUT/serial.log")" -eq 1 ]] || {
  echo 'qemu-ata-eio: expected exactly one guest-visible ATA write error' >&2
  exit 1
}
[[ "$(grep -Fc 'ATA_RECOVERY_RESET_OK' "$OUT/serial.log")" -eq 1 ]] || {
  echo 'qemu-ata-eio: expected exactly one successful ATA reset' >&2
  exit 1
}
[[ "$(grep -Fc 'ATA_RECOVERY_REVALIDATE_OK command=identify capacity=stable' "$OUT/serial.log")" -eq 1 ]] || {
  echo 'qemu-ata-eio: expected exactly one successful IDENTIFY revalidation' >&2
  exit 1
}
[[ "$(grep -Fc 'ATA_RECOVERY_RETRY_OK op=write attempts=2' "$OUT/serial.log")" -eq 1 ]] || {
  echo 'qemu-ata-eio: expected exactly one successful write retry' >&2
  exit 1
}

grep -Fq "$PROOF_TEXT" "$OUT/serial.log"
grep -Fq 'ZENOVFS_FSCK_OK' "$OUT/serial.log"
grep -Fq 'ZENOVOS_UI_READY' "$OUT/serial.log"
! grep -Eq 'ATA_RECOVERY_REVALIDATE_FAILED|KERNEL PANIC|DOUBLE FAULT|ASSERT|PS2_MOUSE_UNAVAILABLE' "$OUT/serial.log"

reason="$(grep -F 'ATA_IO_ERROR op=write reason=' "$OUT/serial.log" | head -n 1 | sed 's/.*reason=//' | tr -d '\r')"
[[ -n "$reason" ]] || { echo 'qemu-ata-eio: empty normalized ATA reason' >&2; exit 1; }
printf 'ATA_EIO_RETRY_QEMU_OK injected=EIO operation=write reason=%s resets=1 revalidations=1 attempts=2 proof=%s\n' \
  "$reason" "$PROOF_TEXT" | tee "$OUT/summary.log"
