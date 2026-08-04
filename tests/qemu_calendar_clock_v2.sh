#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-calendar-clock-v2}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-calendar-clock-v2.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_serial_file() {
  local serial="$1" text="$2"
  for _ in $(seq 1 1400); do
    [[ -f "$serial" ]] && grep -Fq "$text" "$serial" && return 0
    sleep 0.1
  done
  return 1
}

send_key() {
  echo "sendkey $1 20"
  sleep 0.08
}

send_text_key() {
  echo "sendkey $1 20"
  sleep 0.18
}

capture() {
  local name="$1"
  local file="$(cd "$OUT" && pwd)/${name}.ppm"
  sleep 1
  echo "screendump $file"
  sleep 0.25
}

open_start_result() {
  local query="$1"
  send_key f8
  local index character
  for ((index = 0; index < ${#query}; ++index)); do
    character="${query:index:1}"
    send_key "$character"
  done
  send_key ret
}

type_calendar_event() {
  for key in m e e t spc shift-2 0 9 shift-semicolon 3 0 spc shift-equal 9 0 spc shift-1 w e e k l y; do
    send_text_key "$key"
  done
}

type_alarm() {
  for key in 1 2 shift-semicolon 0 0 spc t e s t spc a l a r m spc shift-1 o n c e; do
    send_text_key "$key"
  done
}

SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
controller_phase1() {
  wait_for_serial_file "$SERIAL1" "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL1" "UI_CALENDAR_CLOCK_V2_READY calendar=timed+duration+recurrence+edit alarms=persistent timers=three stopwatch=laps" || { echo quit; return 1; }

  open_start_result calendar
  wait_for_serial_file "$SERIAL1" "UI_CALENDAR_OPEN_APP_OK" || { echo quit; return 1; }
  send_key f4
  type_calendar_event
  capture calendar-input
  send_key ret
  wait_for_serial_file "$SERIAL1" "UI_CALENDAR_V2_ADD_OK" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL1" "UI_CALENDAR_V2_SAVE_OK" || { echo quit; return 1; }
  capture calendar-timed
  send_key f5
  send_key ret
  wait_for_serial_file "$SERIAL1" "UI_CALENDAR_V2_EDIT_OK" || { echo quit; return 1; }
  capture calendar-edited
  send_key esc

  open_start_result clock
  wait_for_serial_file "$SERIAL1" "UI_CLOCK_OPEN_APP_OK" || { echo quit; return 1; }
  send_key f1
  sleep 1.2
  send_key l
  wait_for_serial_file "$SERIAL1" "UI_CLOCK_V2_LAP_OK" || { echo quit; return 1; }
  send_key f3
  send_key right
  send_key f3
  send_key f3
  send_key f4
  wait_for_serial_file "$SERIAL1" "UI_CLOCK_V2_TIMER_RUNNING" || { echo quit; return 1; }
  capture clock-lap-timers

  send_key a
  type_alarm
  capture clock-alarm-input
  send_key ret
  wait_for_serial_file "$SERIAL1" "UI_CLOCK_V2_ALARM_ADD_OK" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL1" "UI_CLOCK_V2_ALARMS_SAVE_OK" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL1" "UI_CLOCK_V2_ALARM_DUE" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL1" "UI_BACKGROUND_CLOCK_REFRESH_OK" || { echo quit; return 1; }
  capture clock-alarm-due
  send_key esc
  echo quit
}

set +e
controller_phase1 | timeout 220s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -rtc base=2026-08-04T12:00:00 \
  -serial "file:$SERIAL1" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor-phase1.log" 2>"$OUT/qemu-phase1.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-calendar-clock-v2: phase1 failed with status $status" >&2
  cat "$OUT/monitor-phase1.log" >&2 || true
  cat "$OUT/qemu-phase1.stderr" >&2 || true
  cat "$SERIAL1" >&2 || true
  exit 1
fi

SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
controller_phase2() {
  wait_for_serial_file "$SERIAL2" "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL2" "UI_CALENDAR_V2_LOAD_OK" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL2" "UI_CLOCK_V2_ALARMS_LOAD_OK" || { echo quit; return 1; }
  open_start_result calendar
  wait_for_serial_file "$SERIAL2" "UI_CALENDAR_OPEN_APP_OK" || { echo quit; return 1; }
  capture calendar-persisted
  send_key esc
  open_start_result clock
  wait_for_serial_file "$SERIAL2" "UI_CLOCK_OPEN_APP_OK" || { echo quit; return 1; }
  capture clock-alarm-persisted
  send_key esc
  echo quit
}

set +e
controller_phase2 | timeout 100s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -rtc base=2026-08-04T12:02:00 \
  -serial "file:$SERIAL2" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor-phase2.log" 2>"$OUT/qemu-phase2.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-calendar-clock-v2: phase2 failed with status $status" >&2
  cat "$OUT/monitor-phase2.log" >&2 || true
  cat "$OUT/qemu-phase2.stderr" >&2 || true
  cat "$SERIAL2" >&2 || true
  exit 1
fi

check_ppm() {
  local file="$1"
  [[ -s "$file" ]] || { echo "qemu-calendar-clock-v2: missing screenshot: $file" >&2; return 1; }
  local dimensions
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  [[ "$dimensions" == "1024 768" ]] || { echo "qemu-calendar-clock-v2: wrong dimensions for $file: $dimensions" >&2; return 1; }
}

for image in calendar-input calendar-timed calendar-edited clock-lap-timers clock-alarm-input clock-alarm-due calendar-persisted clock-alarm-persisted; do
  check_ppm "$OUT/$image.ppm"
done

for marker in UI_CALENDAR_V2_ADD_OK UI_CALENDAR_V2_EDIT_OK UI_CALENDAR_V2_SAVE_OK \
  UI_CLOCK_V2_LAP_OK UI_CLOCK_V2_TIMER_SET_OK UI_CLOCK_V2_TIMER_RUNNING \
  UI_CLOCK_V2_ALARM_ADD_OK UI_CLOCK_V2_ALARM_DUE UI_CLOCK_V2_ALARMS_SAVE_OK UI_BACKGROUND_CLOCK_REFRESH_OK; do
  grep -Fq "$marker" "$SERIAL1" || { echo "qemu-calendar-clock-v2: missing phase1 marker: $marker" >&2; exit 1; }
done

grep -Fq "UI_CALENDAR_V2_LOAD_OK" "$SERIAL2"
grep -Fq "UI_CLOCK_V2_ALARMS_LOAD_OK" "$SERIAL2"
test ! -s "$OUT/qemu-phase1.stderr"
test ! -s "$OUT/qemu-phase2.stderr"

printf 'qemu-calendar-clock-v2: OK calendar=timed+duration+weekly+edit+reboot clock=laps+three-timers+persistent-once-alarm+idle-delivery+reboot runtime=%s\n' "$RUNTIME_DATA"
