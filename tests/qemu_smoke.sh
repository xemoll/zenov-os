#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu}"
RECOVERY_IMAGE="${4:-}"
BOOT_MARKER="ZENOVOS_BOOT_OK"
UI_MARKER="ZENOVOS_UI_READY"
STORAGE_MARKER="ZENOVFS_MOUNT_OK"
PMM_MARKER="PMM_OK"
PAGING_MARKER="PAGING_OK"
PROCESS_MARKER="PROCESS_ABI_0_1_1_OK"
LONG_INPUT_MARKER="longinputend511ok"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial*.log "$OUT"/screenshot.ppm "$OUT"/monitor*.log "$OUT"/qemu*.stderr
SCREENSHOT="$(cd "$OUT" && pwd)/screenshot.ppm"

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 300); do
    [[ -f "$file" ]] && grep -q "$text" "$file" && return 0
    sleep 0.1
  done
  return 1
}
wait_for_count() {
  local file="$1" text="$2" expected="$3"
  for _ in $(seq 1 300); do
    [[ -f "$file" ]] && [[ "$(grep -c "$text" "$file" || true)" -ge "$expected" ]] && return 0
    sleep 0.1
  done
  return 1
}
send_text() {
  local text="$1" char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower 10" ;;
      ' ') echo "sendkey spc 10" ;;
      '.') echo "sendkey dot 10" ;;
      '-') echo "sendkey minus 10" ;;
      '_') echo "sendkey shift-minus 10" ;;
      '/') echo "sendkey slash 10" ;;
      *) echo "qemu-smoke: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.012
  done
}
send_command() { send_text "$1"; echo "sendkey ret 10"; }
wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" \
    && wait_for_serial "$serial" "$PMM_MARKER" \
    && wait_for_serial "$serial" "PMM_STRESS_OK" \
    && wait_for_serial "$serial" "$PAGING_MARKER" \
    && wait_for_serial "$serial" "HEAP_REUSE_OK" \
    && wait_for_serial "$serial" "HEAP_COALESCE_OK" \
    && wait_for_serial "$serial" "HEAP_INVALID_FREE_BLOCKED" \
    && wait_for_serial "$serial" "HEAP_STRESS_OK" \
    && wait_for_serial "$serial" "$STORAGE_MARKER" \
    && wait_for_serial "$serial" "$PROCESS_MARKER" \
    && wait_for_serial "$serial" "$UI_MARKER" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_first() {
  local serial="$1" prompt_count=1
  wait_for_boot "$serial" || { echo quit; return 1; }
  sleep 0.3; echo "screendump $SCREENSHOT"; sleep 0.2
  echo "sendkey f1 10"; wait_for_serial "$serial" "COMMAND REFERENCE" || { echo quit; return 1; }; echo "sendkey f4 10"; sleep 0.2

  send_command "vm"; wait_for_serial "$serial" "VIRTUAL MEMORY" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  local long_payload; long_payload="$(printf 'a%.0s' {1..160})${LONG_INPUT_MARKER}"
  send_command "echo $long_payload"; wait_for_serial "$serial" "$LONG_INPUT_MARKER" || { echo quit; return 1; }
  send_command "write PERSIST.TXT PERSISTENCE_0_1_1_OK"; wait_for_serial "$serial" "WRITE_OK" || { echo quit; return 1; }

  send_command "run HELLO"; wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  send_command "run FILEIO.ELF"; wait_for_serial "$serial" "FILEIO_ELF_OK" || { echo quit; return 1; }; wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }
  send_command "run ARGS.ELF alpha beta"; wait_for_serial "$serial" "PROCESS_ARGV_OK" || { echo quit; return 1; }; wait_for_serial "$serial" "SYSCALL_ERRORS_OK" || { echo quit; return 1; }; wait_for_serial "$serial" "SYSCALL_POINTER_GUARD_OK" || { echo quit; return 1; }

  send_command "run CONSOLE.ELF"; wait_for_serial "$serial" "CONSOLE_READ_READY" || { echo quit; return 1; }
  send_text "zenov"; echo "sendkey ret 10"; wait_for_serial "$serial" "CONSOLE_READ_SYSCALL_OK" || { echo quit; return 1; }

  prompt_count="$(grep -c "$PROMPT" "$serial" || true)"
  send_command "run PROTECT.ELF"
  wait_for_serial "$serial" "USER_WRITE_TO_TEXT_BLOCKED" || { echo quit; return 1; }
  wait_for_serial "$serial" "PAGE_FAULT_DIAGNOSTICS_OK" || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }

  prompt_count="$(grep -c "$PROMPT" "$serial" || true)"
  send_command "run KACCESS.ELF"
  wait_for_serial "$serial" "USER_KERNEL_ACCESS_BLOCKED" || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }

  send_command "run ZENOVAPP.ZEX"
  wait_for_serial "$serial" "ZENOV_SOURCE_APP_RING3_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOV_COMPILER_ABI_MATCH_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_second() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "cat PERSIST.TXT"; wait_for_serial "$serial" "PERSISTENCE_0_1_1_OK" || { echo quit; return 1; }
  send_command "cat /data/apps/userio.txt"; wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  send_command "stat /data/apps/userio.txt"; wait_for_serial "$serial" "Checksum" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_recovery() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOVFS_INTERRUPTED_WRITE_RECOVERED" || { echo quit; return 1; }
  send_command "cat /data/config/system.ini"
  wait_for_serial "$serial" "recovery=committed" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

run_phase() {
  local controller="$1" serial="$2" monitor="$3" stderr="$4" data_image="$5"
  set +e
  "$controller" "$serial" | timeout 50s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$data_image,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -display none -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?; set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-smoke: phase failed with status $status" >&2; cat "$stderr" >&2 || true; cat "$serial" >&2 || true; return 1
  fi
}

[[ -n "$RECOVERY_IMAGE" && -f "$RECOVERY_IMAGE" ]] || { echo "qemu-smoke: recovery image is required" >&2; exit 1; }
SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
SERIAL3="$(cd "$OUT" && pwd)/serial-recovery.log"
run_phase controller_first "$SERIAL1" "$OUT/monitor-phase1.log" "$OUT/qemu-phase1.stderr" "$DATA_IMAGE"
run_phase controller_second "$SERIAL2" "$OUT/monitor-phase2.log" "$OUT/qemu-phase2.stderr" "$DATA_IMAGE"
run_phase controller_recovery "$SERIAL3" "$OUT/monitor-recovery.log" "$OUT/qemu-recovery.stderr" "$RECOVERY_IMAGE"
cat "$SERIAL1" "$SERIAL2" "$SERIAL3" > "$OUT/serial.log"

for marker in \
  "$BOOT_MARKER" "$PMM_MARKER" "PMM_STRESS_OK" "$PAGING_MARKER" "HEAP_REUSE_OK" "HEAP_COALESCE_OK" "HEAP_INVALID_FREE_BLOCKED" "HEAP_STRESS_OK" \
  "$STORAGE_MARKER" "$PROCESS_MARKER" "$UI_MARKER" "$LONG_INPUT_MARKER" "WRITE_OK" "HELLO_ZEX_0_1_1_OK" \
  "FILEIO_ELF_OK" "FILE_SYSCALL_PERSIST_OK" "PROCESS_ARGV_OK" "SYSCALL_ERRORS_OK" "SYSCALL_POINTER_GUARD_OK" "CONSOLE_READ_SYSCALL_OK" \
  "PAGE_PROTECTION_OK" "USER_WRITE_TO_TEXT_BLOCKED" "USER_KERNEL_ACCESS_BLOCKED" "PAGE_FAULT_DIAGNOSTICS_OK" "USER_FAULT_RETURNED_TO_SHELL" \
  "ZENOV_SOURCE_APP_RING3_OK" "ZENOV_COMPILER_ABI_MATCH_OK" "ZENOVFS_INTERRUPTED_WRITE_RECOVERED" "recovery=committed" "ZENOVFS_FSCK_OK"; do
  grep -q "$marker" "$OUT/serial.log" || { echo "qemu-smoke: missing marker: $marker" >&2; exit 1; }
done

[[ "$(grep -c 'APP_EXIT code=0' "$OUT/serial.log")" -ge 5 ]] || { echo "qemu-smoke: successful applications did not all exit cleanly" >&2; exit 1; }
if grep -q "Application could not be loaded" "$OUT/serial.log"; then echo "qemu-smoke: shell reported an application load failure" >&2; exit 1; fi
[[ "$(grep -c 'PERSISTENCE_0_1_1_OK' "$OUT/serial.log")" -ge 2 ]] || { echo "qemu-smoke: shell persistence marker missing across reboot" >&2; exit 1; }
[[ "$(grep -c 'FILE_SYSCALL_PERSIST_OK' "$OUT/serial.log")" -ge 2 ]] || { echo "qemu-smoke: userspace file payload missing across reboot" >&2; exit 1; }
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK 0.1.1 protected-pages stress-tested-memory argv+console guarded-syscalls recoverable-faults transactional-fs kernel-recovery zenov-source-app serial=%s screenshot=%s\n' "$OUT/serial.log" "$SCREENSHOT"
