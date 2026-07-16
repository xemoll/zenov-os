#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu}"
BOOT_MARKER="ZENOVOS_BOOT_OK"
UI_MARKER="ZENOVOS_UI_READY"
STORAGE_MARKER="ZENOVFS_MOUNT_OK"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial*.log "$OUT"/screenshot.ppm "$OUT"/monitor*.log "$OUT"/qemu*.stderr "$OUT"/zenov-data-corrupt.img
SCREENSHOT="$(cd "$OUT" && pwd)/screenshot.ppm"

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 200); do
    [[ -f "$file" ]] && grep -q "$text" "$file" && return 0
    sleep 0.1
  done
  return 1
}

send_text() {
  local text="$1" char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower" ;;
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
send_command() { send_text "$1"; echo "sendkey ret"; }

wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" \
    && wait_for_serial "$serial" "PMM_OK" \
    && wait_for_serial "$serial" "PAGING_OK" \
    && wait_for_serial "$serial" "$STORAGE_MARKER" \
    && wait_for_serial "$serial" "ZENOVFS_BOOT_FSCK_OK" \
    && wait_for_serial "$serial" "CONFIG_LOAD_OK theme=graphite" \
    && wait_for_serial "$serial" "USER_FAULT_ISOLATION_OK" \
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

  send_command "write PERSIST.TXT PERSISTENCE_0_1_1_R2_OK"
  wait_for_serial "$serial" "WRITE_OK" || { echo quit; return 1; }

  send_command "run HELLO"
  wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "APP_EXIT code=0" || { echo quit; return 1; }

  send_command "run FILEIO.ELF"
  wait_for_serial "$serial" "FILEIO_ELF_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }

  send_command "run FAULT.ELF"
  wait_for_serial "$serial" "FAULT_ELF_TRIGGER" || { echo quit; return 1; }
  wait_for_serial "$serial" "APP_FAULT_RECOVERED vector=6" || { echo quit; return 1; }
  wait_for_serial "$serial" "KERNEL_SURVIVED_USER_FAULT" || { echo quit; return 1; }

  send_command "status"
  wait_for_serial "$serial" "SYSTEM STATUS" || { echo quit; return 1; }
  wait_for_serial "$serial" "Core services are running" || { echo quit; return 1; }

  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

controller_second() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "cat PERSIST.TXT"
  wait_for_serial "$serial" "PERSISTENCE_0_1_1_R2_OK" || { echo quit; return 1; }
  send_command "cat /data/apps/userio.txt"
  wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }
  send_command "cat /data/config/system.ini"
  wait_for_serial "$serial" "theme=graphite" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

controller_degraded() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "PMM_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "PAGING_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "Storage: ZenovFS mount failed" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOVFS_BOOT_FSCK_SKIPPED" || { echo quit; return 1; }
  wait_for_serial "$serial" "CONFIG_DEFAULT storage-offline" || { echo quit; return 1; }
  wait_for_serial "$serial" "USER_FAULT_ISOLATION_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "$UI_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  send_command "mount"
  wait_for_serial "$serial" "/dev/ata0     /data    unavailable" || { echo quit; return 1; }
  send_command "status"
  wait_for_serial "$serial" "SYSTEM STATUS" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

run_phase() {
  local controller="$1" serial="$2" monitor="$3" stderr="$4" data_image="$5"
  set +e
  "$controller" "$serial" | timeout 30s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$data_image,format=raw,if=ide,index=0,media=disk" \
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
SERIAL3="$(cd "$OUT" && pwd)/serial-degraded.log"
run_phase controller_first "$SERIAL1" "$OUT/monitor-phase1.log" "$OUT/qemu-phase1.stderr" "$DATA_IMAGE"
run_phase controller_second "$SERIAL2" "$OUT/monitor-phase2.log" "$OUT/qemu-phase2.stderr" "$DATA_IMAGE"

CORRUPT_IMAGE="$(cd "$OUT" && pwd)/zenov-data-corrupt.img"
cp "$DATA_IMAGE" "$CORRUPT_IMAGE"
printf '\000' | dd of="$CORRUPT_IMAGE" bs=1 seek=0 count=1 conv=notrunc status=none
run_phase controller_degraded "$SERIAL3" "$OUT/monitor-degraded.log" "$OUT/qemu-degraded.stderr" "$CORRUPT_IMAGE"
cat "$SERIAL1" "$SERIAL2" "$SERIAL3" > "$OUT/serial.log"

for marker in "$BOOT_MARKER" PMM_OK PAGING_OK "$STORAGE_MARKER" ZENOVFS_BOOT_FSCK_OK \
              "CONFIG_LOAD_OK theme=graphite" USER_FAULT_ISOLATION_OK "$UI_MARKER" \
              "COMMAND REFERENCE" WRITE_OK HELLO_ZEX_0_1_1_OK FILEIO_ELF_OK \
              FAULT_ELF_TRIGGER "APP_FAULT_RECOVERED vector=6" KERNEL_SURVIVED_USER_FAULT \
              "SYSTEM STATUS" ZENOVFS_FSCK_OK "Storage: ZenovFS mount failed" \
              ZENOVFS_BOOT_FSCK_SKIPPED "CONFIG_DEFAULT storage-offline"; do
  grep -q "$marker" "$OUT/serial.log"
done
[[ "$(grep -c 'PERSISTENCE_0_1_1_R2_OK' "$OUT/serial.log")" -ge 2 ]]
[[ "$(grep -c 'FILE_SYSCALL_PERSIST_OK' "$OUT/serial.log")" -ge 2 ]]
[[ "$(grep -c 'CONFIG_LOAD_OK theme=graphite' "$OUT/serial.log")" -ge 2 ]]
[[ "$(grep -c 'ZENOVOS_BOOT_OK' "$OUT/serial.log")" -ge 3 ]]
if grep -q "ZENOVOS KERNEL PANIC" "$OUT/serial.log"; then
  echo "qemu-smoke: a recoverable failure escalated to kernel panic" >&2
  exit 1
fi
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK revision2 fault-isolation verified-storage degraded-boot persistence serial=%s screenshot=%s\n' "$OUT/serial.log" "$SCREENSHOT"
