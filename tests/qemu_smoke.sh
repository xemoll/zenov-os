#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu}"
BOOT_MARKER="ZENOVOS_BOOT_OK"
UI_MARKER="ZENOVOS_UI_READY"
STORAGE_MARKER="ZENOVFS_MOUNT_OK"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial*.log "$OUT"/screenshot.ppm "$OUT"/monitor*.log "$OUT"/qemu*.stderr
SCREENSHOT="$(cd "$OUT" && pwd)/screenshot.ppm"

wait_for_serial() {
  local file="$1"
  local text="$2"
  for _ in $(seq 1 160); do
    [[ -f "$file" ]] && grep -q "$text" "$file" && return 0
    sleep 0.1
  done
  return 1
}

send_text() {
  local text="$1"
  local char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char" ;;
      [A-Z])
        lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"
        echo "sendkey shift-$lower"
        ;;
      ' ') echo "sendkey spc" ;;
      '.') echo "sendkey dot" ;;
      '-') echo "sendkey minus" ;;
      '_') echo "sendkey shift-minus" ;;
      '/') echo "sendkey slash" ;;
      *) echo "qemu-smoke: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() {
  send_text "$1"
  echo "sendkey ret"
}

wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" \
    && wait_for_serial "$serial" "$STORAGE_MARKER" \
    && wait_for_serial "$serial" "$UI_MARKER" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_first() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  sleep 0.3
  echo "screendump $SCREENSHOT"
  sleep 0.2

  echo "sendkey f1"
  wait_for_serial "$serial" "COMMAND REFERENCE" || { echo quit; return 1; }
  echo "sendkey f4"
  sleep 0.2

  send_command "write PERSIST.TXT PERSISTENCE_OK"
  wait_for_serial "$serial" "WRITE_OK" || { echo quit; return 1; }

  send_command "run HELLO"
  wait_for_serial "$serial" "HELLO_ZEX_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "APP_EXIT code=0" || { echo quit; return 1; }

  sleep 0.2
  echo quit
}

controller_second() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "cat PERSIST.TXT"
  wait_for_serial "$serial" "PERSISTENCE_OK" || { echo quit; return 1; }
  send_command "stat PERSIST.TXT"
  wait_for_serial "$serial" "Checksum" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

run_phase() {
  local controller="$1"
  local serial="$2"
  local monitor="$3"
  local stderr="$4"
  set +e
  "$controller" "$serial" | timeout 25s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$DATA_IMAGE,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -display none \
    -serial "file:$serial" \
    -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-smoke: phase failed with status $status" >&2
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    return 1
  fi
}

SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
run_phase controller_first "$SERIAL1" "$OUT/monitor-phase1.log" "$OUT/qemu-phase1.stderr"
run_phase controller_second "$SERIAL2" "$OUT/monitor-phase2.log" "$OUT/qemu-phase2.stderr"
cat "$SERIAL1" "$SERIAL2" > "$OUT/serial.log"

grep -q "$BOOT_MARKER" "$OUT/serial.log"
grep -q "$STORAGE_MARKER" "$OUT/serial.log"
grep -q "$UI_MARKER" "$OUT/serial.log"
grep -q "COMMAND REFERENCE" "$OUT/serial.log"
grep -q "WRITE_OK" "$OUT/serial.log"
grep -q "HELLO_ZEX_OK" "$OUT/serial.log"
grep -q "APP_EXIT code=0" "$OUT/serial.log"
[[ "$(grep -c 'PERSISTENCE_OK' "$OUT/serial.log")" -ge 2 ]] || {
  echo "qemu-smoke: persistence marker was not observed before and after reboot" >&2
  exit 1
}
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK storage-persistence ring3-zex serial=%s screenshot=%s\n' "$OUT/serial.log" "$SCREENSHOT"
