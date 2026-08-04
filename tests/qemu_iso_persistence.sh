#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
ISO_IMAGE="${1:-build/ZenovOS-0.1.1-x86.iso}"
OUT="${2:-build/qemu/iso-live}"
PROMPT='zenov> '
PAYLOAD='ISO_LIVE_SESSION_OK'

for tool in "$QEMU" grep timeout sleep tr; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "qemu-iso-live: required tool not found: $tool" >&2
    exit 1
  }
done
[[ -s "$ISO_IMAGE" ]] || { echo "qemu-iso-live: missing ISO: $ISO_IMAGE" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
OUT_ABS="$(cd "$OUT" && pwd)"
SERIAL="$OUT_ABS/serial.log"
MONITOR="$OUT_ABS/monitor.log"
STDERR="$OUT_ABS/qemu.stderr"

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 700); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-iso-live: timed out waiting for marker: $text" >&2
  tail -200 "$file" >&2 || true
  return 1
}

send_text() {
  local text="$1" char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower 10" ;;
      ' ') echo "sendkey spc 10" ;;
      '.') echo "sendkey dot 10" ;;
      '-') echo "sendkey minus 10" ;;
      '_') echo "sendkey shift-minus 10" ;;
      '/') echo "sendkey slash 10" ;;
      *) echo "qemu-iso-live: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.012
  done
}

send_command() {
  send_text "$1"
  echo 'sendkey ret 10'
}

controller() {
  wait_for_serial "$SERIAL" ZENOVOS_BOOT_OK || { echo quit; return 1; }
  wait_for_serial "$SERIAL" ZENOVFS_LIVE_IMAGE_OK || { echo quit; return 1; }
  wait_for_serial "$SERIAL" 'ZENOVFS_LIVE_READY mode=ram-overlay persistence=session' || { echo quit; return 1; }
  wait_for_serial "$SERIAL" GRAPHICAL_DESKTOP_READY || { echo quit; return 1; }
  wait_for_serial "$SERIAL" ZENOVOS_UI_READY || { echo quit; return 1; }
  wait_for_serial "$SERIAL" "$PROMPT" || { echo quit; return 1; }

  send_command "write LIVE-SESSION.TXT $PAYLOAD"
  wait_for_serial "$SERIAL" WRITE_OK || { echo quit; return 1; }
  send_command 'cat LIVE-SESSION.TXT'
  wait_for_serial "$SERIAL" "$PAYLOAD" || { echo quit; return 1; }
  send_command fsck
  wait_for_serial "$SERIAL" ZENOVFS_FSCK_OK || { echo quit; return 1; }
  send_command sync
  wait_for_serial "$SERIAL" SYNC_OK || { echo quit; return 1; }
  sleep 0.4
  echo quit
}

set +e
controller | timeout 180s "$QEMU" \
  -machine pc,vmport=off \
  -m 64M \
  -vga std \
  -drive "file=$ISO_IMAGE,format=raw,if=ide,index=2,media=cdrom,readonly=on" \
  -boot order=d,strict=on \
  -display none \
  -serial "file:$SERIAL" \
  -monitor stdio \
  -no-reboot \
  -no-shutdown \
  >"$MONITOR" 2>"$STDERR"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-iso-live: QEMU phase failed with status $status" >&2
  cat "$MONITOR" >&2 || true
  cat "$STDERR" >&2 || true
  cat "$SERIAL" >&2 || true
  exit 1
fi

grep -Fq ZENOVFS_LIVE_IMAGE_OK "$SERIAL"
grep -Fq 'ZENOVFS_STORAGE_MODE live-iso' "$SERIAL"
grep -Fq WRITE_OK "$SERIAL"
grep -Fq "$PAYLOAD" "$SERIAL"
grep -Fq ZENOVFS_FSCK_OK "$SERIAL"
grep -Fq SYNC_OK "$SERIAL"

printf 'qemu-iso-live: OK iso=%s payload=%s storage=ram-overlay external-disks=0\n' \
  "$ISO_IMAGE" "$PAYLOAD"
