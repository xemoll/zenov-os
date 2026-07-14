#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
IMAGE="${1:-build/zenov-os.img}"
OUT="${2:-build/qemu}"
MARKER="ZENOVOS_BOOT_OK"
mkdir -p "$OUT"
rm -f "$OUT/serial.log" "$OUT/screenshot.ppm" "$OUT/monitor.log" "$OUT/qemu.stderr" "$OUT/marker.ok"
SCREENSHOT="$(cd "$OUT" && pwd)/screenshot.ppm"
SERIAL="$(cd "$OUT" && pwd)/serial.log"
MARKER_FILE="$(cd "$OUT" && pwd)/marker.ok"

controller() {
  for _ in $(seq 1 120); do
    if [[ -f "$SERIAL" ]] && grep -q "$MARKER" "$SERIAL"; then
      : > "$MARKER_FILE"
      break
    fi
    sleep 0.1
  done
  if [[ ! -f "$MARKER_FILE" ]]; then
    echo quit
    return 1
  fi
  echo "screendump $SCREENSHOT"
  sleep 0.5
  echo quit
}

set +e
controller | timeout 15s "$QEMU" \
  -drive "file=$IMAGE,format=raw,if=floppy" \
  -boot a -m 32M -display none \
  -serial "file:$SERIAL" \
  -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e

if [[ $status -ne 0 ]]; then
  echo "qemu-smoke: QEMU/controller failed with status $status" >&2
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$SERIAL" >&2 || true
  exit 1
fi
grep -q "$MARKER" "$SERIAL" || { echo "qemu-smoke: serial marker missing" >&2; exit 1; }
grep -q "Kernel online" "$SERIAL" || { echo "qemu-smoke: kernel-online marker missing" >&2; exit 1; }
grep -q "zenov> " "$SERIAL" || { echo "qemu-smoke: shell prompt missing" >&2; exit 1; }
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK serial=%s screenshot=%s\n' "$SERIAL" "$SCREENSHOT"
