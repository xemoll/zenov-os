#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-files-ui}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img

SERIAL="$(cd "$OUT" && pwd)/serial.log"
ROOT_SCREENSHOT="$(cd "$OUT" && pwd)/files-root.ppm"
APPS_SCREENSHOT="$(cd "$OUT" && pwd)/files-apps.ppm"
PREVIEW_SCREENSHOT="$(cd "$OUT" && pwd)/files-preview.ppm"
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-files-ui.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_serial() {
  local text="$1"
  for _ in $(seq 1 500); do
    [[ -f "$SERIAL" ]] && grep -q "$text" "$SERIAL" && return 0
    sleep 0.1
  done
  return 1
}

controller() {
  wait_for_serial "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial "UI_FILES_BROWSER_OK" || { echo quit; return 1; }
  wait_for_serial "zenov> " || { echo quit; return 1; }

  echo "sendkey f6 10"
  wait_for_serial "UI_KEYBOARD_NAV_OK" || { echo quit; return 1; }
  sleep 0.25
  echo "screendump $ROOT_SCREENSHOT"
  sleep 0.2

  echo "sendkey ret 10"
  wait_for_serial "UI_FILES_NAV_OK" || { echo quit; return 1; }
  sleep 0.25
  echo "screendump $APPS_SCREENSHOT"
  sleep 0.2

  echo "sendkey ret 10"
  wait_for_serial "UI_FILES_PREVIEW_OK" || { echo quit; return 1; }
  sleep 0.25
  echo "screendump $PREVIEW_SCREENSHOT"
  sleep 0.2

  echo "sendkey backspace 10"
  echo "sendkey f5 10"
  sleep 0.2
  echo quit
}

set +e
controller | timeout 45s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-files-ui: QEMU failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$SERIAL" >&2 || true
  exit 1
fi

for marker in UI_FILES_BROWSER_OK UI_KEYBOARD_NAV_OK UI_FILES_NAV_OK UI_FILES_PREVIEW_OK; do
  grep -q "$marker" "$SERIAL" || { echo "qemu-files-ui: missing marker: $marker" >&2; exit 1; }
done
for screenshot in "$ROOT_SCREENSHOT" "$APPS_SCREENSHOT" "$PREVIEW_SCREENSHOT"; do
  [[ -s "$screenshot" ]] || { echo "qemu-files-ui: missing screenshot: $screenshot" >&2; exit 1; }
done

printf 'qemu-files-ui: OK browser=%s root=%s apps=%s preview=%s\n' \
  "$SERIAL" "$ROOT_SCREENSHOT" "$APPS_SCREENSHOT" "$PREVIEW_SCREENSHOT"
