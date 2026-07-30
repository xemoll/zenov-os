#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-system-apps}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-system-apps.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for() {
  local file="$1" text="$2"
  for _ in $(seq 1 1800); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  return 1
}

marker_count() {
  local file="$1" text="$2"
  if [[ ! -f "$file" ]]; then printf '0\n'; return; fi
  grep -Fc "$text" "$file" 2>/dev/null || true
}

wait_for_marker_count() {
  local file="$1" text="$2" expected="$3" current
  for _ in $(seq 1 1800); do
    current="$(marker_count "$file" "$text")"
    [[ "$current" =~ ^[0-9]+$ ]] && (( current >= expected )) && return 0
    sleep 0.1
  done
  return 1
}

sendkey_and_wait_frame() {
  local serial="$1" key="$2" before
  before="$(marker_count "$serial" 'UI_FRAME_PRESENTED')"
  echo "sendkey $key 10"
  wait_for_marker_count "$serial" 'UI_FRAME_PRESENTED' "$((before + 1))"
}

capture() {
  local file="$(cd "$OUT" && pwd)/$1.ppm"
  echo "screendump $file"
  for _ in $(seq 1 100); do [[ -s "$file" ]] && return 0; sleep 0.1; done
  return 1
}

first_controller() {
  local serial="$1"
  wait_for "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for "$serial" 'UI_SYSTEM_APPS_READY clock=yes calendar=yes notes=zenovfs notepad=zenovfs' || { echo quit; return 1; }
  echo 'sendkey f8 10'; sleep 0.2
  echo 'sendkey down 10'; echo 'sendkey down 10'; echo 'sendkey down 10'; sleep 0.15
  echo 'sendkey ret 10'
  wait_for "$serial" 'UI_PRODUCTIVITY_OPEN_OK' || { echo quit; return 1; }
  wait_for "$serial" 'UI_PRODUCTIVITY_PAGE Clock' || { echo quit; return 1; }
  capture clock || { echo quit; return 1; }

  sendkey_and_wait_frame "$serial" tab || { echo quit; return 1; }
  wait_for "$serial" 'UI_PRODUCTIVITY_PAGE Calendar' || { echo quit; return 1; }
  capture calendar || { echo quit; return 1; }

  sendkey_and_wait_frame "$serial" ret || { echo quit; return 1; }
  wait_for "$serial" 'UI_PRODUCTIVITY_PAGE Notes' || { echo quit; return 1; }
  capture notes || { echo quit; return 1; }
  for key in z e n o v minus n o t e; do echo "sendkey $key 10"; done
  echo 'sendkey f2 10'
  wait_for "$serial" 'UI_PRODUCTIVITY_SAVE_OK path=/docs/note-' || { echo quit; return 1; }

  sendkey_and_wait_frame "$serial" tab || { echo quit; return 1; }
  wait_for "$serial" 'UI_PRODUCTIVITY_PAGE Notepad' || { echo quit; return 1; }
  capture notepad || { echo quit; return 1; }
  for key in s c r a t c h; do echo "sendkey $key 10"; done
  echo 'sendkey f2 10'
  wait_for "$serial" 'UI_PRODUCTIVITY_SAVE_OK path=/docs/scratchpad.txt' || { echo quit; return 1; }
  echo quit
}

run_phase() {
  local phase="$1" controller="$2"
  local serial="$(cd "$OUT" && pwd)/serial-$phase.log"
  set +e
  "$controller" "$serial" | timeout 360s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 64M -machine pc,vmport=off -vga none -device VGA,vgamem_mb=64 -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$OUT/monitor-$phase.log" 2>"$OUT/qemu-$phase.stderr"
  local status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    cat "$OUT/monitor-$phase.log" >&2 || true
    cat "$OUT/qemu-$phase.stderr" >&2 || true
    cat "$serial" >&2 || true
    return "$status"
  fi
  test ! -s "$OUT/qemu-$phase.stderr"
}

second_controller() {
  local serial="$1"
  wait_for "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for "$serial" 'UI_SYSTEM_APPS_READY clock=yes calendar=yes notes=zenovfs notepad=zenovfs' || { echo quit; return 1; }
  echo 'sendkey f8 10'; sleep 0.2
  echo 'sendkey down 10'; echo 'sendkey down 10'; echo 'sendkey down 10'; sleep 0.15
  echo 'sendkey ret 10'
  wait_for "$serial" 'UI_PRODUCTIVITY_OPEN_OK' || { echo quit; return 1; }
  sendkey_and_wait_frame "$serial" tab || { echo quit; return 1; }
  sendkey_and_wait_frame "$serial" tab || { echo quit; return 1; }
  sendkey_and_wait_frame "$serial" tab || { echo quit; return 1; }
  wait_for "$serial" 'UI_PRODUCTIVITY_PAGE Notepad' || { echo quit; return 1; }
  wait_for "$serial" 'UI_PRODUCTIVITY_LOAD_OK path=/docs/scratchpad.txt bytes=19' || { echo quit; return 1; }
  echo quit
}

run_phase first first_controller
run_phase reboot second_controller
for name in clock calendar notes notepad; do
  test -s "$OUT/$name.ppm"
  test "$(sed -n '2p' "$OUT/$name.ppm" | tr -d '\r')" = '1024 768'
done
test "$(sha256sum "$OUT"/{clock,calendar,notes,notepad}.ppm | awk '{print $1}' | sort -u | wc -l)" -eq 4
grep -Fq 'UI_PRODUCTIVITY_SAVE_OK path=/docs/scratchpad.txt' "$OUT/serial-first.log"
grep -Fq 'UI_PRODUCTIVITY_LOAD_OK path=/docs/scratchpad.txt bytes=19' "$OUT/serial-reboot.log"
echo 'SYSTEM_APPS_QEMU_OK apps=clock,calendar,notes,notepad persistence=reboot frames=synchronized'
