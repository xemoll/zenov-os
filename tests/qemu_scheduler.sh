#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/scheduler}"
mkdir -p "$OUT"
rm -f "$OUT"/serial.log "$OUT"/monitor.log "$OUT"/qemu.stderr "$OUT"/runtime.img
cp "$DATA_IMAGE" "$OUT/runtime.img"
SERIAL="$(cd "$OUT" && pwd)/serial.log"
RUNTIME="$(cd "$OUT" && pwd)/runtime.img"

wait_for() {
  local marker="$1"
  for _ in $(seq 1 700); do
    [[ -f "$SERIAL" ]] && grep -Fq "$marker" "$SERIAL" && return 0
    sleep 0.1
  done
  echo "qemu-scheduler: missing marker: $marker" >&2
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
      '&') echo "sendkey shift-7 8" ;;
      *) echo "qemu-scheduler: unsupported key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}
send_command() { send_text "$1"; echo "sendkey ret 8"; }
controller() {
  wait_for "PREEMPTIVE_SCHEDULER_OK hz=100 tasks=8" || { echo quit; return 1; }
  wait_for "SCHED_POLICY_OK model=priority-rr priorities=4 aging=50 quantum=3-6" || { echo quit; return 1; }
  wait_for "PROCESS_ADDRESS_SPACE_OK per-task-pte=yes pmm-backed=yes" || { echo quit; return 1; }
  wait_for "KERNEL_IDENTITY_MAP_OK bytes=134217728 supervisor-only=yes" || { echo quit; return 1; }
  wait_for "USER_HIGH_HALF_WINDOW_OK base=0x40000000 bytes=1048576" || { echo quit; return 1; }
  wait_for "ZENOVOS_UI_READY" || { echo quit; return 1; }
  wait_for "zenov> " || { echo quit; return 1; }
  send_command "run HELLO preempt-a & HELLO preempt-b"
  wait_for "TASK_CREATED pid=1 app=/apps/hello.zex" || { echo quit; return 1; }
  wait_for "TASK_CREATED pid=2 app=/apps/hello.zex" || { echo quit; return 1; }
  wait_for "SCHED_BATCH_START tasks=2" || { echo quit; return 1; }
  wait_for "PREEMPT_A_START" || { echo quit; return 1; }
  wait_for "PREEMPT_B_START" || { echo quit; return 1; }
  wait_for "PREEMPT_A_DONE" || { echo quit; return 1; }
  wait_for "PREEMPT_B_DONE" || { echo quit; return 1; }
  wait_for "SCHED_CONTEXT_SWITCH" || { echo quit; return 1; }
  wait_for "SCHED_BATCH_COMPLETE tasks=2 completed=2 faulted=0" || { echo quit; return 1; }
  wait_for "PREEMPTIVE_MULTITASKING_OK isolation=per-address-space" || { echo quit; return 1; }
  send_command "ps"
  wait_for "TASK SCHEDULER" || { echo quit; return 1; }
  sleep 0.5
  echo quit
}

set +e
controller | timeout 120s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$RUNTIME,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 64M -machine pc,vmport=off -vga std -display none \
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

awk '/PREEMPT_B_START/ {b=NR} /PREEMPT_A_DONE/ {a=NR} END {exit !(b && a && b < a)}' "$SERIAL"
grep -Eq 'SCHED_BATCH_COMPLETE tasks=2 completed=2 faulted=0 switches=[1-9][0-9]* preemptions=[1-9][0-9]*' "$SERIAL"
grep -Fq 'SCHED_CONTEXT_SWITCH from=1 to=2 reason=timer' "$SERIAL"
! grep -Fq 'ZENOVOS KERNEL PANIC' "$SERIAL"
! grep -Fq 'SCHED_BATCH_CREATE_FAILED' "$SERIAL"
printf 'qemu-scheduler: OK tasks=2 policy=priority-rr preemption=timer address-space=per-task\n'
