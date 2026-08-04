#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
ONCE_CONFIG="${3:-tests/blkdebug/ata-read-eio-once.conf}"
ALWAYS_CONFIG="${4:-tests/blkdebug/ata-read-eio-always.conf}"
OUT="${5:-build/qemu/ata-read-faults}"
PROMPT='zenov> '

mkdir -p "$OUT"
rm -rf "$OUT/recovered" "$OUT/exhausted" "$OUT/summary.log"
mkdir -p "$OUT/recovered" "$OUT/exhausted"

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-900}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-ata-read-faults: missing serial marker: $text" >&2
  return 1
}

send_text() {
  local text="$1" char lower
  local i
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="${char,,}"; echo "sendkey shift-$lower 10" ;;
      ' ') echo 'sendkey spc 10' ;;
      '-') echo 'sendkey minus 10' ;;
      *) echo "qemu-ata-read-faults: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() {
  send_text "$1"
  echo 'sendkey ret 10'
}

controller_recovered() {
  local serial="$1"
  wait_for_serial "$serial" 'ATA_IO_ERROR op=read reason=command-aborted' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ATA_RECOVERY_RESET_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ATA_RECOVERY_REVALIDATE_OK command=identify capacity=stable' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ATA_RECOVERY_RETRY_OK op=read attempts=2' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  send_command 'fsck'
  wait_for_serial "$serial" 'ZENOVFS_FSCK_OK' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

controller_exhausted() {
  local serial="$1"
  wait_for_serial "$serial" 'ATA_RECOVERY_RETRY_EXHAUSTED op=read attempts=2 status=command-aborted' || { echo quit; return 1; }
  wait_for_serial "$serial" 'Storage: attached ZenovFS mount failed status=io-error' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_EXTERNAL_FAIL_CLOSED' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS KERNEL PANIC' || { echo quit; return 1; }
  wait_for_serial "$serial" 'Persistent signed policy transaction recovery failed.' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

run_case() {
  local name="$1" config="$2" controller="$3"
  local case_out="$OUT/$name"
  local runtime="$case_out/runtime.img"
  local serial="$case_out/serial.log"
  local monitor="$case_out/monitor.log"
  local stderr="$case_out/qemu.stderr"

  cp "$DATA_IMAGE" "$runtime"
  cmp "$DATA_IMAGE" "$runtime"
  sync -f "$runtime"
  cmp "$DATA_IMAGE" "$runtime"

  local config_abs runtime_abs serial_abs
  config_abs="$(cd "$(dirname "$config")" && pwd)/$(basename "$config")"
  runtime_abs="$(cd "$case_out" && pwd)/runtime.img"
  serial_abs="$(cd "$case_out" && pwd)/serial.log"

  # raw-format emits BLKDBG_READ_AIO to its child. Therefore blkdebug must
  # sit below raw in the block graph: IDE -> raw -> blkdebug -> file.
  set +e
  "$controller" "$serial_abs" | timeout 120s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -blockdev "driver=file,node-name=${name}-file,filename=$runtime_abs,cache.direct=off,cache.no-flush=off" \
    -blockdev "driver=blkdebug,node-name=${name}-debug,config=$config_abs,image=${name}-file" \
    -blockdev "driver=raw,node-name=${name}-raw,file=${name}-debug" \
    -device "ide-hd,id=${name}-disk,drive=${name}-raw,bus=ide.0,unit=0" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial_abs" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?
  set -e

  if [[ $status -ne 0 ]]; then
    echo "qemu-ata-read-faults: $name failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    exit 1
  fi
  [[ ! -s "$stderr" ]] || {
    echo "qemu-ata-read-faults: $name produced QEMU stderr" >&2
    cat "$stderr" >&2
    exit 1
  }
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" && -f "$ONCE_CONFIG" && -f "$ALWAYS_CONFIG" ]] || {
  echo 'qemu-ata-read-faults: boot image, data image and both blkdebug configs are required' >&2
  exit 2
}

grep -Fq 'event = "read_aio"' "$ONCE_CONFIG"
grep -Fq 'iotype = "read"' "$ONCE_CONFIG"
grep -Fq 'sector = "8"' "$ONCE_CONFIG"
grep -Fq 'once = "on"' "$ONCE_CONFIG"
grep -Fq 'event = "read_aio"' "$ALWAYS_CONFIG"
grep -Fq 'iotype = "read"' "$ALWAYS_CONFIG"
grep -Fq 'sector = "8"' "$ALWAYS_CONFIG"
! grep -Fq 'once = "on"' "$ALWAYS_CONFIG"

run_case recovered "$ONCE_CONFIG" controller_recovered
run_case exhausted "$ALWAYS_CONFIG" controller_exhausted

recovered="$OUT/recovered/serial.log"
exhausted="$OUT/exhausted/serial.log"

[[ "$(grep -Fc 'ATA_IO_ERROR op=read reason=command-aborted' "$recovered")" -eq 1 ]]
[[ "$(grep -Fc 'ATA_RECOVERY_RESET_OK' "$recovered")" -eq 1 ]]
[[ "$(grep -Fc 'ATA_RECOVERY_REVALIDATE_OK command=identify capacity=stable' "$recovered")" -eq 1 ]]
[[ "$(grep -Fc 'ATA_RECOVERY_RETRY_OK op=read attempts=2' "$recovered")" -eq 1 ]]
! grep -Fq 'ATA_RECOVERY_RETRY_EXHAUSTED' "$recovered"
grep -Fq 'ZENOVFS_MOUNT_OK' "$recovered"
grep -Fq 'ZENOVFS_FSCK_OK' "$recovered"
grep -Fq 'ZENOVOS_UI_READY' "$recovered"
! grep -Eq 'KERNEL PANIC|DOUBLE FAULT|ASSERT|PS2_MOUSE_UNAVAILABLE' "$recovered"

[[ "$(grep -Fc 'ATA_IO_ERROR op=read reason=command-aborted' "$exhausted")" -eq 2 ]]
[[ "$(grep -Fc 'ATA_RECOVERY_RESET_OK' "$exhausted")" -eq 1 ]]
[[ "$(grep -Fc 'ATA_RECOVERY_REVALIDATE_OK command=identify capacity=stable' "$exhausted")" -eq 1 ]]
[[ "$(grep -Fc 'ATA_RECOVERY_RETRY_EXHAUSTED op=read attempts=2 status=command-aborted' "$exhausted")" -eq 1 ]]
! grep -Fq 'ATA_RECOVERY_RETRY_OK op=read' "$exhausted"
grep -Fq 'Storage: attached ZenovFS mount failed status=io-error' "$exhausted"
[[ "$(grep -Fc 'ZENOVFS_EXTERNAL_FAIL_CLOSED' "$exhausted")" -eq 1 ]]
[[ "$(grep -Fc 'ZENOVOS KERNEL PANIC' "$exhausted")" -eq 1 ]]
[[ "$(grep -Fc 'Persistent signed policy transaction recovery failed.' "$exhausted")" -eq 1 ]]
! grep -Fq 'ZENOVFS_MOUNT_OK' "$exhausted"
! grep -Fq 'ZENOVOS_UI_READY' "$exhausted"
! grep -Fq 'ZENOVFS_LIVE_READY' "$exhausted"

printf 'ATA_READ_FAULT_MATRIX_OK sector=8 recovered_attempts=2 exhausted_attempts=2 resets_each=1 ui_after_recovery=1 ui_after_exhaustion=0 fail_closed=typed security_panic=1\n' \
  | tee "$OUT/summary.log"
