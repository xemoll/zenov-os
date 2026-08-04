#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-notes-v2}"
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.ppm "$OUT"/*.stderr "$OUT"/*.img

RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-notes-v2.img"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

wait_for_marker() {
  local serial="$1" marker="$2"
  for _ in $(seq 1 1200); do
    [[ -f "$serial" ]] && grep -Fq "$marker" "$serial" && return 0
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

open_notes() {
  echo "sendkey f8 10"
  for key in n o t e s; do echo "sendkey $key 10"; done
  echo "sendkey ret 10"
}

phase1_controller() {
  local serial="$1" out="$2"
  wait_for_marker "$serial" "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_marker "$serial" "UI_NOTES_V2_READY cursor=insert+delete navigation=arrows+home+end history=undo8+redo8 viewport=logical-lines" || { echo quit; return 1; }
  open_notes
  wait_for_marker "$serial" "UI_NOTES_OPEN_APP_OK" || { echo quit; return 1; }
  echo "sendkey f3 10"
  wait_for_marker "$serial" "UI_NOTES_V2_OPEN_OK" || { echo quit; return 1; }

  for key in a l p h a; do echo "sendkey $key 10"; done
  echo "sendkey ret 10"
  for key in g a m a; do echo "sendkey $key 10"; done
  echo "sendkey left 10"
  echo "sendkey m 10"
  echo "sendkey home 10"
  echo "sendkey shift-x 10"
  echo "sendkey end 10"
  echo "sendkey shift-y 10"
  echo "sendkey ret 10"
  for key in d e l t a x; do echo "sendkey $key 10"; done
  echo "sendkey backspace 10"
  echo "sendkey shift-q 10"
  echo "sendkey home 10"
  echo "sendkey right 10"
  echo "sendkey right 10"
  echo "sendkey delete 10"
  echo "sendkey l 10"
  echo "sendkey end 10"
  echo "sendkey z 10"

  echo "sendkey f5 10"
  wait_for_marker "$serial" "UI_NOTES_V2_UNDO_OK" || { echo quit; return 1; }
  capture "$out" notes-v2-undo
  echo "sendkey f6 10"
  wait_for_marker "$serial" "UI_NOTES_V2_REDO_OK" || { echo quit; return 1; }
  capture "$out" notes-v2-redo
  echo "sendkey f2 10"
  wait_for_marker "$serial" "UI_NOTES_V2_SAVE_OK" || { echo quit; return 1; }
  capture "$out" notes-v2-saved
  echo "sendkey esc 10"
  wait_for_marker "$serial" "UI_NOTES_V2_CLOSE_OK" || { echo quit; return 1; }
  echo quit
}

phase2_controller() {
  local serial="$1" out="$2"
  wait_for_marker "$serial" "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for_marker "$serial" "UI_NOTES_V2_READY cursor=insert+delete navigation=arrows+home+end history=undo8+redo8 viewport=logical-lines" || { echo quit; return 1; }
  open_notes
  wait_for_marker "$serial" "UI_NOTES_OPEN_APP_OK" || { echo quit; return 1; }
  echo "sendkey f3 10"
  wait_for_marker "$serial" "UI_NOTES_V2_OPEN_OK" || { echo quit; return 1; }
  capture "$out" notes-v2-persisted
  echo quit
}

run_phase() {
  local phase="$1" serial="$2" stderr="$3" monitor="$4"
  set +e
  if [[ "$phase" == "phase1" ]]; then
    phase1_controller "$serial" "$OUT" | timeout 180s "$QEMU" \
      -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
      -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
      -boot a -m 32M -machine pc,vmport=off -vga std -display none \
      -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown >"$monitor" 2>"$stderr"
  else
    phase2_controller "$serial" "$OUT" | timeout 180s "$QEMU" \
      -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
      -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
      -boot a -m 32M -machine pc,vmport=off -vga std -display none \
      -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown >"$monitor" 2>"$stderr"
  fi
  local status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-notes-v2: $phase failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    exit 1
  fi
}

SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
run_phase phase1 "$SERIAL1" "$OUT/qemu-phase1.stderr" "$OUT/monitor-phase1.log"
run_phase phase2 "$SERIAL2" "$OUT/qemu-phase2.stderr" "$OUT/monitor-phase2.log"

check_ppm() {
  local file="$1"
  [[ -s "$file" ]] || { echo "qemu-notes-v2: missing screenshot $file" >&2; return 1; }
  local dimensions
  dimensions="$(sed -n '2p' "$file" | tr -d '\r')"
  [[ "$dimensions" == "1024 768" ]] || { echo "qemu-notes-v2: wrong dimensions $dimensions for $file" >&2; return 1; }
}

for image in notes-v2-undo notes-v2-redo notes-v2-saved notes-v2-persisted; do check_ppm "$OUT/$image.ppm"; done

grep -Fq "UI_NOTES_V2_UNDO_OK" "$SERIAL1"
grep -Fq "UI_NOTES_V2_REDO_OK" "$SERIAL1"
grep -Fq "UI_NOTES_V2_SAVE_OK" "$SERIAL1"
grep -Fq "UI_NOTES_V2_CLOSE_OK" "$SERIAL1"
grep -Fq "UI_NOTES_V2_OPEN_OK" "$SERIAL2"
test ! -s "$OUT/qemu-phase1.stderr"
test ! -s "$OUT/qemu-phase2.stderr"

printf 'qemu-notes-v2: OK cursor=middle-insert+backspace+delete navigation=home+end history=undo+redo persistence=reboot serial1=%s serial2=%s runtime=%s screenshots=%s/*.ppm\n' \
  "$SERIAL1" "$SERIAL2" "$RUNTIME_DATA" "$OUT"
