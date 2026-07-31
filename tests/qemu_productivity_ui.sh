#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-productivity-ui}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img

SERIAL="$(cd "$OUT" && pwd)/serial.log"
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-productivity.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_serial() {
  local text="$1"
  for _ in $(seq 1 1200); do
    [[ -f "$SERIAL" ]] && grep -Fq "$text" "$SERIAL" && return 0
    sleep 0.1
  done
  return 1
}

wait_for_count() {
  local text="$1" expected="$2"
  for _ in $(seq 1 1200); do
    [[ -f "$SERIAL" ]] && [[ "$(grep -Fc "$text" "$SERIAL" || true)" -ge "$expected" ]] && return 0
    sleep 0.1
  done
  return 1
}

capture() {
  local name="$1"
  local file="$(cd "$OUT" && pwd)/${name}.ppm"
  # Application markers are emitted before refresh_desktop() returns. Allow the
  # completed frame to reach the physical framebuffer before screendump.
  sleep 0.35
  echo "screendump $file"
  sleep 0.15
}

open_start_result() {
  local query="$1"
  echo "sendkey f8 10"
  local index character
  for ((index = 0; index < ${#query}; ++index)); do
    character="${query:index:1}"
    echo "sendkey $character 10"
  done
  echo "sendkey ret 10"
}

type_task_fixture() {
  # ship #P1 #D-2099-12-31
  for key in s h i p spc shift-3 p 1 spc shift-3 d minus 2 0 9 9 minus 1 2 minus 3 1; do
    echo "sendkey $key 10"
  done
}

controller() {
  wait_for_serial "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial "UI_PRODUCTIVITY_APPS_READY notes=yes calendar=yes clock=yes scratchpad=yes" || { echo quit; return 1; }
  wait_for_serial "UI_PRODUCTIVITY_TASKS_READY markdown=yes metadata=priority+due+waiting" || { echo quit; return 1; }

  open_start_result notes
  wait_for_serial "UI_NOTES_OPEN_APP_OK" || { echo quit; return 1; }
  capture notes-browser

  echo "sendkey f1 10"
  wait_for_count "UI_NOTES_OPEN_OK" 1 || { echo quit; return 1; }
  echo "sendkey t 10"
  echo "sendkey e 10"
  echo "sendkey s 10"
  echo "sendkey t 10"
  echo "sendkey f2 10"
  wait_for_count "UI_NOTES_SAVE_OK" 1 || { echo quit; return 1; }
  capture notes-editor
  echo "sendkey esc 10"

  echo "sendkey f3 10"
  wait_for_count "UI_NOTES_OPEN_OK" 2 || { echo quit; return 1; }
  echo "sendkey s 10"
  echo "sendkey c 10"
  echo "sendkey r 10"
  echo "sendkey a 10"
  echo "sendkey t 10"
  echo "sendkey c 10"
  echo "sendkey h 10"
  echo "sendkey f2 10"
  wait_for_count "UI_NOTES_SAVE_OK" 2 || { echo quit; return 1; }
  capture scratchpad
  echo "sendkey esc 10"

  echo "sendkey f4 10"
  wait_for_count "UI_NOTES_OPEN_OK" 3 || { echo quit; return 1; }
  echo "sendkey f2 10"
  wait_for_count "UI_NOTES_SAVE_OK" 3 || { echo quit; return 1; }
  capture daily-note
  echo "sendkey esc 10"
  echo "sendkey esc 10"
  wait_for_serial "UI_PRODUCTIVITY_CLOSE_OK" || { echo quit; return 1; }

  open_start_result tasks
  wait_for_serial "UI_TASKS_OPEN_APP_OK" || { echo quit; return 1; }
  echo "sendkey f4 10"
  type_task_fixture
  echo "sendkey ret 10"
  wait_for_serial "UI_TASKS_ADD_OK" || { echo quit; return 1; }
  capture tasks-open
  echo "sendkey ret 10"
  wait_for_serial "UI_TASKS_TOGGLE_OK" || { echo quit; return 1; }
  echo "sendkey f3 10"
  capture tasks-done
  echo "sendkey esc 10"

  open_start_result calendar
  wait_for_serial "UI_CALENDAR_OPEN_APP_OK" || { echo quit; return 1; }
  echo "sendkey f4 10"
  echo "sendkey m 10"
  echo "sendkey e 10"
  echo "sendkey e 10"
  echo "sendkey t 10"
  echo "sendkey ret 10"
  wait_for_serial "UI_CALENDAR_SAVE_OK" || { echo quit; return 1; }
  capture calendar-event
  echo "sendkey esc 10"

  open_start_result clock
  wait_for_serial "UI_CLOCK_OPEN_APP_OK" || { echo quit; return 1; }
  echo "sendkey f1 10"
  wait_for_serial "UI_CLOCK_STOPWATCH_RUNNING" || { echo quit; return 1; }
  echo "sendkey f3 10"
  wait_for_serial "UI_CLOCK_TIMER_SET_OK" || { echo quit; return 1; }
  echo "sendkey f4 10"
  wait_for_serial "UI_CLOCK_TIMER_RUNNING" || { echo quit; return 1; }
  capture clock-running
  echo "sendkey esc 10"

  echo quit
}

set +e
controller | timeout 170s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-productivity-ui: QEMU failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$SERIAL" >&2 || true
  exit 1
fi

check_ppm() {
  local file="$1"
  [[ -s "$file" ]] || { echo "qemu-productivity-ui: missing screenshot: $file" >&2; return 1; }
  local dimensions
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  [[ "$dimensions" == "1024 768" ]] || {
    echo "qemu-productivity-ui: wrong dimensions for $file: $dimensions" >&2
    return 1
  }
}

for image in notes-browser notes-editor scratchpad daily-note tasks-open tasks-done calendar-event clock-running; do
  check_ppm "$OUT/$image.ppm"
done

for marker in \
  "UI_PRODUCTIVITY_APPS_READY notes=yes calendar=yes clock=yes scratchpad=yes" \
  "UI_PRODUCTIVITY_TASKS_READY markdown=yes metadata=priority+due+waiting" \
  UI_PRODUCTIVITY_STORAGE_OK UI_NOTES_OPEN_APP_OK UI_NOTES_OPEN_OK UI_NOTES_SAVE_OK \
  UI_TASKS_OPEN_APP_OK UI_TASKS_SCAN_OK UI_TASKS_ADD_OK UI_TASKS_TOGGLE_OK \
  UI_CALENDAR_OPEN_APP_OK UI_CALENDAR_SAVE_OK UI_CLOCK_OPEN_APP_OK \
  UI_CLOCK_STOPWATCH_RUNNING UI_CLOCK_TIMER_SET_OK UI_CLOCK_TIMER_RUNNING; do
  grep -Fq "$marker" "$SERIAL" || { echo "qemu-productivity-ui: missing marker: $marker" >&2; exit 1; }
done

test "$(grep -Fc 'UI_TASKS_ADD_OK' "$SERIAL")" -eq 1
test "$(grep -Fc 'UI_TASKS_TOGGLE_OK' "$SERIAL")" -eq 1

printf 'qemu-productivity-ui: OK notes=local-markdown+scratch+daily tasks=aggregate+add+toggle calendar=persistent-events clock=rtc+stopwatch+countdown start-search=yes serial=%s screenshots=%s runtime=%s\n' \
  "$SERIAL" "$OUT/*.ppm" "$RUNTIME_DATA"
