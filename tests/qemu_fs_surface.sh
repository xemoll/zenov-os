#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/fs-surface}"
PROMPT='zenov> '

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
  echo "qemu-fs-surface: missing serial marker: $text" >&2
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
      '_') echo 'sendkey shift-minus 10' ;;
      '.') echo 'sendkey dot 10' ;;
      '/') echo 'sendkey slash 10' ;;
      *) echo "qemu-fs-surface: unsupported test key: $char" >&2; return 1 ;;
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
  wait_for_serial "$serial" 'ZENOVFS_SURFACE_CONTRACT_OK version=1' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }

  send_command 'cat /missing.txt'
  wait_for_serial "$serial" 'FS_COMMAND_FAILED action=cat status=not-found class=missing' || { echo quit; return 1; }

  send_command 'cd /apps/hello.zex'
  wait_for_serial "$serial" 'FS_COMMAND_FAILED action=cd status=wrong-type class=caller' || { echo quit; return 1; }

  send_command 'mkdir /missing/child'
  wait_for_serial "$serial" 'FS_COMMAND_FAILED action=mkdir status=parent-missing class=missing' || { echo quit; return 1; }

  send_command 'write /apps/hello.zex probe'
  wait_for_serial "$serial" 'FS_COMMAND_FAILED action=write status=permission-denied class=policy' || { echo quit; return 1; }

  send_command 'rm /missing.txt'
  wait_for_serial "$serial" 'FS_COMMAND_FAILED action=remove status=not-found class=missing' || { echo quit; return 1; }

  send_command 'sync'
  wait_for_serial "$serial" 'SYNC_OK' || { echo quit; return 1; }

  send_command 'fsck'
  wait_for_serial "$serial" 'ZENOVFS_FSCK_OK' || { echo quit; return 1; }

  send_command 'disk status'
  wait_for_serial "$serial" 'ZENOVFS_RESULT_COUNTERS_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'BLOCK_DEVICE_STATUS_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_SURFACE_API_OK version=1' || { echo quit; return 1; }

  sleep 0.2
  echo quit
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" ]] || {
  echo 'qemu-fs-surface: boot and data images are required' >&2
  exit 2
}

cp "$DATA_IMAGE" "$OUT/runtime.img"
cmp "$DATA_IMAGE" "$OUT/runtime.img"
sync -f "$OUT/runtime.img"
SERIAL_ABS="$(cd "$OUT" && pwd)/serial.log"
RUNTIME_ABS="$(cd "$OUT" && pwd)/runtime.img"

set +e
controller "$SERIAL_ABS" | timeout 150s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_ABS,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL_ABS" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e

if [[ $status -ne 0 ]]; then
  echo "qemu-fs-surface: QEMU/controller failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$OUT/serial.log" >&2 || true
  exit 1
fi

[[ ! -s "$OUT/qemu.stderr" ]] || {
  echo 'qemu-fs-surface: non-empty QEMU stderr' >&2
  cat "$OUT/qemu.stderr" >&2
  exit 1
}

test "$(grep -Fc 'FS_COMMAND_FAILED action=' "$OUT/serial.log")" -eq 5
grep -Fq 'FS_COMMAND_FAILED action=cat status=not-found class=missing' "$OUT/serial.log"
grep -Fq 'FS_COMMAND_FAILED action=cd status=wrong-type class=caller' "$OUT/serial.log"
grep -Fq 'FS_COMMAND_FAILED action=mkdir status=parent-missing class=missing' "$OUT/serial.log"
grep -Fq 'FS_COMMAND_FAILED action=write status=permission-denied class=policy' "$OUT/serial.log"
grep -Fq 'FS_COMMAND_FAILED action=remove status=not-found class=missing' "$OUT/serial.log"
grep -Fq 'SYNC_OK' "$OUT/serial.log"
grep -Fq 'ZENOVFS_FSCK_OK' "$OUT/serial.log"
grep -Fq 'ZENOVFS_RESULT_COUNTERS_OK' "$OUT/serial.log"
grep -Fq 'ZENOVFS_SURFACE_API_OK version=1' "$OUT/serial.log"
! grep -Eq 'KERNEL PANIC|DOUBLE FAULT|ASSERT|PS2_MOUSE_UNAVAILABLE|ZENOVFS_RESULT_COUNTERS_INVALID' "$OUT/serial.log"

printf 'ZENOVFS_SHELL_RESULT_OK cases=5 classes=caller,missing,policy sync=1 fsck=1\n' \
  | tee "$OUT/summary.log"
