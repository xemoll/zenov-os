#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu}"
BOOT_MARKER="ZENOVOS_BOOT_OK"
UI_MARKER="ZENOVOS_UI_READY"
STORAGE_MARKER="ZENOVFS_MOUNT_OK"
PMM_MARKER="PMM_OK"
PAGING_MARKER="PAGING_OK"
PROCESS_MARKER="PROCESS_ABI_0_1_1_OK"
LONG_INPUT_MARKER="LONG_INPUT_END_511_OK"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial*.log "$OUT"/screenshot.ppm "$OUT"/monitor*.log "$OUT"/qemu*.stderr
SCREENSHOT="$(cd "$OUT" && pwd)/screenshot.ppm"

wait_for_serial() {
  local file="$1"
  local text="$2"
  for _ in $(seq 1 200); do
    [[ -f "$file" ]] && grep -q "$text" "$file" && return 0
    sleep 0.1
  done
  return 1
}

send_text() {
  local text="$1"
  local char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char" ;;
      [A-Z])
        lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"
        echo "sendkey shift-$lower"
        ;;
      ' ') echo "sendkey spc" ;;
      '.') echo "sendkey dot" ;;
      '-') echo "sendkey minus" ;;
      '_') echo "sendkey shift-minus" ;;
      '/') echo "sendkey slash" ;;
      *) echo "qemu-smoke: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() {
  send_text "$1"
  echo "sendkey ret"
}

wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" \
    && wait_for_serial "$serial" "$PMM_MARKER" \
    && wait_for_serial "$serial" "$PAGING_MARKER" \
    && wait_for_serial "$serial" "$STORAGE_MARKER" \
    && wait_for_serial "$serial" "$PROCESS_MARKER" \
    && wait_for_serial "$serial" "$UI_MARKER" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_first() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  sleep 0.3
  echo "screendump $SCREENSHOT"
  sleep 0.2

  echo "sendkey f1"
  wait_for_serial "$serial" "COMMAND REFERENCE" || { echo quit; return 1; }
  echo "sendkey f4"
  sleep 0.2

  send_command "vm"
  wait_for_serial "$serial" "VIRTUAL MEMORY" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }

  local long_payload
  long_payload="LONG_INPUT_$(printf 'A%.0s' {1..160})_END_511_OK"
  send_command "echo $long_payload"
  wait_for_serial "$serial" "$LONG_INPUT_MARKER" || { echo quit; return 1; }

  send_command "write PERSIST.TXT PERSISTENCE_0_1_1_OK"
  wait_for_serial "$serial" "WRITE_OK" || { echo quit; return 1; }

  send_command "run HELLO"
  wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "APP_START_ZEX" || { echo quit; return 1; }

  send_command "run FILEIO.ELF"
  wait_for_serial "$serial" "APP_START_ELF" || { echo quit; return 1; }
  wait_for_serial "$serial" "FILEIO_ELF_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }

  sleep 0.2
  echo quit
}

controller_second() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "cat PERSIST.TXT"
  wait_for_serial "$serial" "PERSISTENCE_0_1_1_OK" || { echo quit; return 1; }
  send_command "cat /data/apps/userio.txt"
  wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  send_command "stat /data/apps/userio.txt"
  wait_for_serial "$serial" "Checksum" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

run_phase() {
  local controller="$1"
  local serial="$2"
  local monitor="$3"
  local stderr="$4"
  set +e
  "$controller" "$serial" | timeout 30s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$DATA_IMAGE,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -display none \
    -serial "file:$serial" \
    -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-smoke: phase failed with status $status" >&2
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    return 1
  fi
}

SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
run_phase controller_first "$SERIAL1" "$OUT/monitor-phase1.log" "$OUT/qemu-phase1.stderr"
run_phase controller_second "$SERIAL2" "$OUT/monitor-phase2.log" "$OUT/qemu-phase2.stderr"
cat "$SERIAL1" "$SERIAL2" > "$OUT/serial.log"

for marker in \
  "$BOOT_MARKER" "$PMM_MARKER" "$PAGING_MARKER" "$STORAGE_MARKER" "$PROCESS_MARKER" "$UI_MARKER" \
  "COMMAND REFERENCE" "VIRTUAL MEMORY" "$LONG_INPUT_MARKER" "WRITE_OK" "HELLO_ZEX_0_1_1_OK" "APP_START_ZEX" \
  "APP_START_ELF" "FILEIO_ELF_OK" "FILE_SYSCALL_PERSIST_OK" "ZENOVFS_FSCK_OK"; do
  grep -q "$marker" "$OUT/serial.log" || { echo "qemu-smoke: missing marker: $marker" >&2; exit 1; }
done

[[ "$(grep -c 'APP_EXIT code=0' "$OUT/serial.log")" -ge 2 ]] || {
  echo "qemu-smoke: both native applications did not exit cleanly" >&2
  exit 1
}
if grep -q "Application could not be loaded" "$OUT/serial.log"; then
  echo "qemu-smoke: shell reported a false load failure after clean application exit" >&2
  exit 1
fi
[[ "$(grep -c 'PERSISTENCE_0_1_1_OK' "$OUT/serial.log")" -ge 2 ]] || {
  echo "qemu-smoke: shell persistence marker was not observed before and after reboot" >&2
  exit 1
}
[[ "$(grep -c 'FILE_SYSCALL_PERSIST_OK' "$OUT/serial.log")" -ge 2 ]] || {
  echo "qemu-smoke: userspace file-syscall payload was not observed before and after reboot" >&2
  exit 1
}
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK 0.1.1 shell>80 paging+pmm zex+elf file-syscalls persistence serial=%s screenshot=%s\n' "$OUT/serial.log" "$SCREENSHOT"
