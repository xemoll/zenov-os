#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-display-ui}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img

SERIAL="$(cd "$OUT" && pwd)/serial.log"
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-display-ui.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_serial() {
  local text="$1"
  for _ in $(seq 1 700); do
    [[ -f "$SERIAL" ]] && grep -q "$text" "$SERIAL" && return 0
    sleep 0.1
  done
  return 1
}

wait_for_count() {
  local text="$1" expected="$2"
  for _ in $(seq 1 700); do
    [[ -f "$SERIAL" ]] && [[ "$(grep -c "$text" "$SERIAL" || true)" -ge "$expected" ]] && return 0
    sleep 0.1
  done
  return 1
}

capture_mode() {
  local name="$1"
  local file="$(cd "$OUT" && pwd)/${name}.ppm"
  echo "screendump $file"
  sleep 0.35
}

controller() {
  wait_for_serial "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial "UI_ADAPTIVE_DISPLAY_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_MODE_OK 1024x768" || { echo quit; return 1; }
  capture_mode desktop-1024x768

  echo "sendkey f9 10"
  wait_for_serial "UI_DISPLAY_MODE_OK 1280x720" || { echo quit; return 1; }
  capture_mode desktop-1280x720

  echo "sendkey f9 10"
  wait_for_serial "UI_DISPLAY_MODE_OK 640x480" || { echo quit; return 1; }
  capture_mode desktop-640x480

  echo "sendkey f9 10"
  wait_for_serial "UI_DISPLAY_MODE_OK 800x600" || { echo quit; return 1; }
  capture_mode desktop-800x600

  echo "sendkey f9 10"
  wait_for_serial "UI_DISPLAY_MODE_OK 1024x600" || { echo quit; return 1; }
  capture_mode desktop-1024x600

  echo "sendkey f9 10"
  wait_for_count "UI_DISPLAY_MODE_OK 1024x768" 2 || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_CYCLE_OK" || { echo quit; return 1; }
  wait_for_serial "UI_DISPLAY_PERSIST_OK" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

set +e
controller | timeout 75s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-display-ui: QEMU failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$SERIAL" >&2 || true
  exit 1
fi

check_ppm() {
  local file="$1" expected="$2" dimensions
  [[ -s "$file" ]] || { echo "qemu-display-ui: missing screenshot: $file" >&2; return 1; }
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  [[ "$dimensions" == "$expected" ]] || {
    echo "qemu-display-ui: wrong dimensions for $file: got '$dimensions', expected '$expected'" >&2
    return 1
  }
}

check_ppm "$OUT/desktop-1024x768.ppm" "1024 768"
check_ppm "$OUT/desktop-1280x720.ppm" "1280 720"
check_ppm "$OUT/desktop-640x480.ppm" "640 480"
check_ppm "$OUT/desktop-800x600.ppm" "800 600"
check_ppm "$OUT/desktop-1024x600.ppm" "1024 600"

for marker in UI_ADAPTIVE_DISPLAY_OK UI_DISPLAY_CYCLE_OK UI_DISPLAY_PERSIST_OK; do
  grep -q "$marker" "$SERIAL" || { echo "qemu-display-ui: missing marker: $marker" >&2; exit 1; }
done

printf 'qemu-display-ui: OK serial=%s screenshots=%s\n' "$SERIAL" "$OUT/desktop-*.ppm"
