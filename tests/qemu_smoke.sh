#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
IMAGE="${1:-build/zenov-os.img}"
OUT="${2:-build/qemu}"
BOOT_MARKER="ZENOVOS_BOOT_OK"
UI_MARKER="ZENOVOS_UI_READY"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT/serial.log" "$OUT/screenshot.ppm" "$OUT/monitor.log" "$OUT/qemu.stderr" "$OUT/marker.ok"
SCREENSHOT="$(cd "$OUT" && pwd)/screenshot.ppm"
SERIAL="$(cd "$OUT" && pwd)/serial.log"
MARKER_FILE="$(cd "$OUT" && pwd)/marker.ok"

wait_for_serial() {
  local text="$1"
  for _ in $(seq 1 100); do
    [[ -f "$SERIAL" ]] && grep -q "$text" "$SERIAL" && return 0
    sleep 0.1
  done
  return 1
}

controller() {
  for _ in $(seq 1 150); do
    if [[ -f "$SERIAL" ]] \
      && grep -q "$BOOT_MARKER" "$SERIAL" \
      && grep -q "$UI_MARKER" "$SERIAL" \
      && grep -q "$PROMPT" "$SERIAL"; then
      : > "$MARKER_FILE"
      break
    fi
    sleep 0.1
  done
  if [[ ! -f "$MARKER_FILE" ]]; then
    echo quit
    return 1
  fi

  sleep 0.4
  echo "screendump $SCREENSHOT"
  sleep 0.4

  # F1 must open the command reference without typed input.
  echo "sendkey f1"
  wait_for_serial "COMMAND REFERENCE" || { echo quit; return 1; }

  # F4 returns to the home view. Then "sta<Tab>" must complete to status.
  echo "sendkey f4"
  sleep 0.2
  echo "sendkey s"
  echo "sendkey t"
  echo "sendkey a"
  echo "sendkey tab"
  echo "sendkey ret"
  wait_for_serial "SYSTEM STATUS" || { echo quit; return 1; }

  sleep 0.2
  echo quit
}

set +e
controller | timeout 20s "$QEMU" \
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
grep -q "$BOOT_MARKER" "$SERIAL" || { echo "qemu-smoke: boot marker missing" >&2; exit 1; }
grep -q "$UI_MARKER" "$SERIAL" || { echo "qemu-smoke: UI-ready marker missing" >&2; exit 1; }
grep -q "Kernel online" "$SERIAL" || { echo "qemu-smoke: kernel-online marker missing" >&2; exit 1; }
grep -q "$PROMPT" "$SERIAL" || { echo "qemu-smoke: shell prompt missing" >&2; exit 1; }
grep -q "COMMAND REFERENCE" "$SERIAL" || { echo "qemu-smoke: F1 shortcut failed" >&2; exit 1; }
grep -q "SYSTEM STATUS" "$SERIAL" || { echo "qemu-smoke: Tab completion/status command failed" >&2; exit 1; }
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK boot/UI/keyboard/completion serial=%s screenshot=%s\n' "$SERIAL" "$SCREENSHOT"