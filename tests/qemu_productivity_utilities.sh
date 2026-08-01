#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-productivity-utilities}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img

RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-utilities.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_serial_file() {
  local serial="$1" text="$2"
  for _ in $(seq 1 1400); do
    [[ -f "$serial" ]] && grep -Fq "$text" "$serial" && return 0
    sleep 0.1
  done
  return 1
}

capture() {
  local out="$1" name="$2"
  local file="$(cd "$out" && pwd)/${name}.ppm"
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

SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
controller_phase1() {
  wait_for_serial_file "$SERIAL1" "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL1" "UI_PRODUCTIVITY_UTILITIES_READY calculator=standard+programmer+date+units reminders=smart+agenda+alarms" || { echo quit; return 1; }

  open_start_result calculator
  wait_for_serial_file "$SERIAL1" "UI_CALCULATOR_OPEN_OK" || { echo quit; return 1; }
  for key in 2 shift-equal 3 shift-8 4; do echo "sendkey $key 10"; done
  echo "sendkey ret 10"
  wait_for_serial_file "$SERIAL1" "UI_CALCULATOR_EVAL_OK" || { echo quit; return 1; }
  capture "$OUT" calculator-standard

  echo "sendkey f2 10"
  echo "sendkey delete 10"
  for key in 0 x f shift-comma shift-comma 2; do echo "sendkey $key 10"; done
  echo "sendkey ret 10"
  for _ in $(seq 1 800); do
    [[ "$(grep -Fc 'UI_CALCULATOR_EVAL_OK' "$SERIAL1" || true)" -ge 2 ]] && break
    sleep 0.1
  done
  [[ "$(grep -Fc 'UI_CALCULATOR_EVAL_OK' "$SERIAL1" || true)" -ge 2 ]] || { echo quit; return 1; }
  capture "$OUT" calculator-programmer

  echo "sendkey f3 10"
  echo "sendkey tab 10"
  echo "sendkey up 10"
  capture "$OUT" calculator-date
  echo "sendkey f4 10"
  echo "sendkey delete 10"
  echo "sendkey 1 10"
  echo "sendkey f6 10"
  echo "sendkey f7 10"
  capture "$OUT" calculator-units
  echo "sendkey esc 10"

  open_start_result reminders
  wait_for_serial_file "$SERIAL1" "UI_REMINDERS_OPEN_OK" || { echo quit; return 1; }
  echo "sendkey f4 10"
  for key in d r i n k spc w a t e r; do echo "sendkey $key 10"; done
  echo "sendkey ret 10"
  wait_for_serial_file "$SERIAL1" "UI_REMINDER_ADD_OK" || { echo quit; return 1; }
  capture "$OUT" reminders-today

  sleep 70
  echo "sendkey down 10"
  wait_for_serial_file "$SERIAL1" "UI_REMINDER_ALARM_DUE" || { echo quit; return 1; }
  capture "$OUT" reminders-alarm
  echo "sendkey s 10"
  wait_for_serial_file "$SERIAL1" "UI_REMINDER_SNOOZE_OK" || { echo quit; return 1; }
  echo "sendkey ret 10"
  wait_for_serial_file "$SERIAL1" "UI_REMINDER_TOGGLE_OK" || { echo quit; return 1; }
  echo "sendkey f5 10"
  capture "$OUT" reminders-done
  echo "sendkey esc 10"
  echo quit
}

set +e
controller_phase1 | timeout 230s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL1" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor-phase1.log" 2>"$OUT/qemu-phase1.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-productivity-utilities: phase1 failed with status $status" >&2
  cat "$OUT/monitor-phase1.log" >&2 || true
  cat "$OUT/qemu-phase1.stderr" >&2 || true
  cat "$SERIAL1" >&2 || true
  exit 1
fi

SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
controller_phase2() {
  wait_for_serial_file "$SERIAL2" "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_serial_file "$SERIAL2" "UI_PRODUCTIVITY_UTILITIES_READY calculator=standard+programmer+date+units reminders=smart+agenda+alarms" || { echo quit; return 1; }
  open_start_result calculator
  wait_for_serial_file "$SERIAL2" "UI_CALCULATOR_OPEN_OK" || { echo quit; return 1; }
  capture "$OUT" calculator-persisted
  echo "sendkey esc 10"
  open_start_result reminders
  wait_for_serial_file "$SERIAL2" "UI_REMINDERS_OPEN_OK" || { echo quit; return 1; }
  echo "sendkey f5 10"
  capture "$OUT" reminders-persisted
  echo quit
}

set +e
controller_phase2 | timeout 125s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL2" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor-phase2.log" 2>"$OUT/qemu-phase2.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "qemu-productivity-utilities: phase2 failed with status $status" >&2
  cat "$OUT/monitor-phase2.log" >&2 || true
  cat "$OUT/qemu-phase2.stderr" >&2 || true
  cat "$SERIAL2" >&2 || true
  exit 1
fi

check_ppm() {
  local file="$1"
  [[ -s "$file" ]] || { echo "qemu-productivity-utilities: missing screenshot: $file" >&2; return 1; }
  local dimensions
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  [[ "$dimensions" == "1024 768" ]] || {
    echo "qemu-productivity-utilities: wrong dimensions for $file: $dimensions" >&2
    return 1
  }
}

for image in calculator-standard calculator-programmer calculator-date calculator-units reminders-today reminders-alarm reminders-done calculator-persisted reminders-persisted; do
  check_ppm "$OUT/$image.ppm"
done

for marker in \
  "UI_PRODUCTIVITY_UTILITIES_READY calculator=standard+programmer+date+units reminders=smart+agenda+alarms" \
  UI_PRODUCTIVITY_UTILITIES_STORAGE_OK UI_CALCULATOR_OPEN_OK UI_CALCULATOR_EVAL_OK \
  UI_CALCULATOR_STATE_SAVE_OK UI_REMINDERS_OPEN_OK UI_REMINDER_ADD_OK UI_REMINDER_ALARM_DUE \
  UI_REMINDER_SNOOZE_OK UI_REMINDER_TOGGLE_OK UI_REMINDERS_SAVE_OK; do
  grep -Fq "$marker" "$SERIAL1" || { echo "qemu-productivity-utilities: missing phase1 marker: $marker" >&2; exit 1; }
done

grep -Fq "UI_CALCULATOR_OPEN_OK" "$SERIAL2"
grep -Fq "UI_REMINDERS_LOAD_OK" "$SERIAL2"
grep -Fq "UI_REMINDERS_OPEN_OK" "$SERIAL2"
test "$(grep -Fc 'UI_CALCULATOR_EVAL_OK' "$SERIAL1")" -eq 2
test "$(grep -Fc 'UI_REMINDER_ADD_OK' "$SERIAL1")" -eq 1
test "$(grep -Fc 'UI_REMINDER_ALARM_DUE' "$SERIAL1")" -eq 1
test ! -s "$OUT/qemu-phase1.stderr"
test ! -s "$OUT/qemu-phase2.stderr"

printf 'qemu-productivity-utilities: OK calculator=standard+programmer+date+units+history reminders=smart+alarm+snooze+reboot agenda=tasks+events+reminders runtime=%s\n' "$RUNTIME_DATA"
