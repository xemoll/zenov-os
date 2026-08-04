#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/kernel-process-lifecycle}"
mkdir -p "$OUT"
rm -f "$OUT"/serial.log "$OUT"/monitor.log "$OUT"/qemu.stderr "$OUT"/runtime.img
cp "$DATA_IMAGE" "$OUT/runtime.img"
SERIAL="$(cd "$OUT" && pwd)/serial.log"
RUNTIME="$(cd "$OUT" && pwd)/runtime.img"

wait_for() {
  local marker="$1"
  for _ in $(seq 1 900); do
    [[ -f "$SERIAL" ]] && grep -Fq "$marker" "$SERIAL" && return 0
    sleep 0.1
  done
  echo "qemu-kernel-process-lifecycle: missing marker: $marker" >&2
  return 1
}
send_text() {
  local text="$1" char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 8" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower 8" ;;
      ' ') echo "sendkey spc 8" ;;
      '-') echo "sendkey minus 8" ;;
      *) echo "qemu-kernel-process-lifecycle: unsupported key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}
send_command() { send_text "$1"; echo "sendkey ret 8"; }
controller() {
  wait_for "PREEMPTIVE_SCHEDULER_OK hz=100 tasks=8" || { echo quit; return 1; }
  wait_for "PROCESS_OBJECT_MANAGER_OK objects=16 handles-per-process=8 pid-generation=yes" || { echo quit; return 1; }
  wait_for "PROCESS_WAIT_MODEL_OK child-zombie=yes handle-wait=yes cleanup=deterministic" || { echo quit; return 1; }
  wait_for "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for "zenov> " || { echo quit; return 1; }
  send_command "proctest"
  wait_for "PROCESS_LIFECYCLE_TRUST_BOUNDARY_OK parent=kernel-rodata child=signed-zenovfs" || { echo quit; return 1; }
  wait_for "TASK_CREATED pid=1 app=/kernel/process-lifecycle-parent parent=0" || { echo quit; return 1; }
  wait_for "PROCESS_PARENT_START" || { echo quit; return 1; }
  wait_for "PROCESS_SPAWN parent=1 child=2" || { echo quit; return 1; }
  wait_for "PROCESS_WAIT_COMPLETE waiter=1 target=2" || { echo quit; return 1; }
  wait_for "PROCESS_CHILD_ONE_REAPED" || { echo quit; return 1; }
  wait_for "PROCESS_SPAWN parent=1 child=258" || { echo quit; return 1; }
  wait_for "PROCESS_WAIT_COMPLETE waiter=1 target=258" || { echo quit; return 1; }
  wait_for "PROCESS_PID_GENERATION_OK" || { echo quit; return 1; }
  wait_for "PROCESS_HANDLE_STALE_BLOCKED" || { echo quit; return 1; }
  wait_for "PROCESS_LIFECYCLE_PROBE_OK" || { echo quit; return 1; }
  wait_for "SCHED_BATCH_COMPLETE tasks=3 completed=3 faulted=0" || { echo quit; return 1; }
  wait_for "PROCESS_LIFECYCLE_RUNTIME_OK spawned=2 reaped=2 handles=closed stale=blocked objects=collected" || { echo quit; return 1; }
  sleep 0.5
  echo quit
}

set +e
controller | timeout 150s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 128M -machine pc,vmport=off -vga std -display none \
  -serial "file:$SERIAL" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$SERIAL" >&2 || true
  exit "$status"
fi

grep -Eq 'PROCESS_SPAWN parent=1 child=2 handle=[1-9][0-9]*' "$SERIAL"
grep -Eq 'PROCESS_SPAWN parent=1 child=258 handle=[1-9][0-9]*' "$SERIAL"
[[ "$(grep -c 'HELLO_ZEX_0_1_1_OK' "$SERIAL")" -eq 2 ]]
! grep -Fq 'PROCESS_LIFECYCLE_PROBE_FAILED' "$SERIAL"
! grep -Fq 'PROCESS_LIFECYCLE_PROBE_CREATE_FAILED' "$SERIAL"
! grep -Fq 'TASK_FAULT_ISOLATED' "$SERIAL"
! grep -Fq 'ZENOVOS KERNEL PANIC' "$SERIAL"
! grep -Fq 'ZENOV_GUARD_TRUST_BASELINE_FAILED' "$SERIAL"
printf 'qemu-kernel-process-lifecycle: OK spawn=2 wait=handle+child pid-generation=1 cleanup=deterministic\n'
