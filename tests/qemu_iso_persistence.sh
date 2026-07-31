#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
QEMU_IMG="${QEMU_IMG:-qemu-img}"
ISO_IMAGE="${1:-build/ZenovOS-0.1.1-x86.iso}"
RAW_DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/iso-persistence}"
PROMPT='zenov> '
PAYLOAD='ISO_VM_PERSISTENCE_OK'

for tool in "$QEMU" "$QEMU_IMG" grep timeout sleep tr; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "qemu-iso-persistence: required tool not found: $tool" >&2
    exit 1
  }
done
[[ -s "$ISO_IMAGE" ]] || { echo "qemu-iso-persistence: missing ISO: $ISO_IMAGE" >&2; exit 1; }
[[ -s "$RAW_DATA_IMAGE" ]] || { echo "qemu-iso-persistence: missing data image: $RAW_DATA_IMAGE" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
OUT_ABS="$(cd "$OUT" && pwd)"
RUNTIME_DISK="$OUT_ABS/ZenovOS-0.1.1-data.qcow2"
"$QEMU_IMG" convert -q -f raw -O qcow2 -o compat=1.1,cluster_size=65536 \
  "$RAW_DATA_IMAGE" "$RUNTIME_DISK"
"$QEMU_IMG" check -q -f qcow2 "$RUNTIME_DISK"

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 700); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-iso-persistence: timed out waiting for marker: $text" >&2
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
      *) echo "qemu-iso-persistence: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.012
  done
}

send_command() {
  send_text "$1"
  echo 'sendkey ret 10'
}

wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" ZENOVOS_BOOT_OK \
    && wait_for_serial "$serial" PMM_OK \
    && wait_for_serial "$serial" PAGING_OK \
    && wait_for_serial "$serial" ZENOVFS_MOUNT_OK \
    && wait_for_serial "$serial" PROCESS_ABI_0_1_1_OK \
    && wait_for_serial "$serial" GRAPHICAL_DESKTOP_READY \
    && wait_for_serial "$serial" ZENOVOS_UI_READY \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_write() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "write ISO-PERSIST.TXT $PAYLOAD"
  wait_for_serial "$serial" WRITE_OK || { echo quit; return 1; }
  send_command sync
  wait_for_serial "$serial" SYNC_OK || { echo quit; return 1; }
  sleep 0.4
  echo quit
}

controller_read() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command 'cat ISO-PERSIST.TXT'
  wait_for_serial "$serial" "$PAYLOAD" || { echo quit; return 1; }
  send_command fsck
  wait_for_serial "$serial" ZENOVFS_FSCK_OK || { echo quit; return 1; }
  send_command sync
  wait_for_serial "$serial" SYNC_OK || { echo quit; return 1; }
  sleep 0.4
  echo quit
}

run_phase() {
  local controller="$1" serial="$2" monitor="$3" stderr="$4"
  set +e
  "$controller" "$serial" | timeout 180s "$QEMU" \
    -machine pc,vmport=off \
    -m 64M \
    -vga std \
    -drive "file=$RUNTIME_DISK,format=qcow2,if=ide,index=0,media=disk" \
    -drive "file=$ISO_IMAGE,format=raw,if=ide,index=2,media=cdrom,readonly=on" \
    -boot order=d,strict=on \
    -display none \
    -serial "file:$serial" \
    -monitor stdio \
    -no-reboot \
    -no-shutdown \
    >"$monitor" 2>"$stderr"
  status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-iso-persistence: phase failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    return 1
  fi
  "$QEMU_IMG" check -q -f qcow2 "$RUNTIME_DISK"
}

SERIAL1="$OUT_ABS/serial-write.log"
SERIAL2="$OUT_ABS/serial-read.log"
run_phase controller_write "$SERIAL1" "$OUT_ABS/monitor-write.log" "$OUT_ABS/qemu-write.stderr"
run_phase controller_read "$SERIAL2" "$OUT_ABS/monitor-read.log" "$OUT_ABS/qemu-read.stderr"
cat "$SERIAL1" "$SERIAL2" > "$OUT_ABS/serial.log"

[[ "$(grep -Fc ZENOVOS_BOOT_OK "$OUT_ABS/serial.log")" -ge 2 ]] || {
  echo "qemu-iso-persistence: expected two successful optical boots" >&2
  exit 1
}
[[ "$(grep -Fc "$PAYLOAD" "$OUT_ABS/serial.log")" -ge 2 ]] || {
  echo "qemu-iso-persistence: persistent payload was not written and replayed" >&2
  exit 1
}
grep -Fq ZENOVFS_FSCK_OK "$SERIAL2"

ROUNDTRIP_RAW="$OUT_ABS/ZenovOS-0.1.1-data-roundtrip.img"
"$QEMU_IMG" convert -q -f qcow2 -O raw "$RUNTIME_DISK" "$ROUNDTRIP_RAW"
if [[ -x build/zenovfs-verify ]]; then
  build/zenovfs-verify "$ROUNDTRIP_RAW"
fi

printf 'qemu-iso-persistence: OK iso=%s disk=%s payload=%s boots=2\n' \
  "$ISO_IMAGE" "$RUNTIME_DISK" "$PAYLOAD"
