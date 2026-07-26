#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
VALID_IMAGE="${2:-build/zenov-data.img}"
MANIFEST_CORRUPT_IMAGE="${3:-build/qemu/zenov-data-zvrt-manifest-corrupt.img}"
DATA_CORRUPT_IMAGE="${4:-build/qemu/zenov-data-zvrt-data-corrupt.img}"
OUT="${5:-build/qemu/zvrt}"
PROMPT='zenov> '
UI_MARKER='ZENOVOS_UI_READY'
mkdir -p "$OUT"
rm -f "$OUT"/*.log "$OUT"/*.stderr "$OUT"/*.img

for required in "$BOOT_IMAGE" "$VALID_IMAGE" "$MANIFEST_CORRUPT_IMAGE" "$DATA_CORRUPT_IMAGE"; do
  [[ -s "$required" ]] || { echo "qemu-zvrt: required image missing: $required" >&2; exit 1; }
done

cp "$VALID_IMAGE" "$OUT/valid-runtime.img"
cp "$MANIFEST_CORRUPT_IMAGE" "$OUT/manifest-corrupt-runtime.img"
cp "$DATA_CORRUPT_IMAGE" "$OUT/data-corrupt-runtime.img"

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 900); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-zvrt: timed out waiting for marker: $text" >&2
  return 1
}

wait_for_count() {
  local file="$1" text="$2" expected="$3"
  for _ in $(seq 1 900); do
    [[ -f "$file" ]] && [[ "$(grep -Fc "$text" "$file" || true)" -ge "$expected" ]] && return 0
    sleep 0.1
  done
  echo "qemu-zvrt: timed out waiting for count $expected: $text" >&2
  return 1
}

send_text() {
  local text="$1" char lower
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower 10" ;;
      ' ') echo 'sendkey spc 10' ;;
      '.') echo 'sendkey dot 10' ;;
      '-') echo 'sendkey minus 10' ;;
      '_') echo 'sendkey shift-minus 10' ;;
      '/') echo 'sendkey slash 10' ;;
      *) echo "qemu-zvrt: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.012
  done
}

send_command() { send_text "$1"; echo 'sendkey ret 10'; }

wait_for_valid_boot() {
  local serial="$1"
  wait_for_serial "$serial" 'ZENOVOS_BOOT_OK' \
    && wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' \
    && wait_for_serial "$serial" 'ZRWP_READY' \
    && wait_for_serial "$serial" 'ZVRT_ROOT_KEY_OK id=d28215ec62269ffc' \
    && wait_for_serial "$serial" 'ZVRT_PSS_SIGNATURE_OK' \
    && wait_for_serial "$serial" 'ZVRT_WORKSPACE_OK address=0x00310000 bytes=4096 supervisor-only=yes' \
    && wait_for_serial "$serial" 'ZVRT_MANIFEST_OK version=1 records=4 chunk=4096 leaves=5' \
    && wait_for_serial "$serial" 'ZVRT_READY' \
    && wait_for_serial "$serial" "$UI_MARKER" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_valid() {
  local serial="$1" prompt_count sync_count
  wait_for_valid_boot "$serial" || { echo quit; return 1; }

  prompt_count="$(grep -Fc "$PROMPT" "$serial" || true)"
  send_command 'run FILEIO.ELF'
  wait_for_serial "$serial" 'ZVRT_READ_OK path=/apps/fileio.elf chunks=2' || { echo quit; return 1; }
  wait_for_serial "$serial" 'FILEIO_ELF_OK' || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }

  prompt_count="$(grep -Fc "$PROMPT" "$serial" || true)"
  send_command 'cat /data/samples/clean.txt'
  wait_for_serial "$serial" 'ZVRT_READ_OK path=/samples/clean.txt chunks=1' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOV_ZMID_CLEAN_VECTOR_V1' || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }

  sync_count="$(grep -Fc 'SYNC_OK' "$serial" || true)"
  send_command 'sync'
  wait_for_count "$serial" 'SYNC_OK' $((sync_count + 1)) || { echo quit; return 1; }
  sleep 0.3
  echo quit
}

controller_manifest_corrupt() {
  local serial="$1"
  wait_for_serial "$serial" 'ZENOVOS_BOOT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZRWP_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZVRT_INIT_FAILED reason=payload-digest' || { echo quit; return 1; }
  wait_for_serial "$serial" 'Signed verified-read manifest validation failed.' || { echo quit; return 1; }
  if grep -Fq "$UI_MARKER" "$serial"; then
    echo 'qemu-zvrt: manifest-corrupt image reached UI' >&2
    echo quit
    return 1
  fi
  echo quit
}

controller_data_corrupt() {
  local serial="$1" prompt_count sync_count verify_count
  wait_for_valid_boot "$serial" || { echo quit; return 1; }
  prompt_count="$(grep -Fc "$PROMPT" "$serial" || true)"
  send_command 'cat /data/samples/clean.txt'
  wait_for_serial "$serial" 'ZVRT_READ_BLOCKED path=/samples/clean.txt reason=file-digest' || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }
  if grep -Fq 'ZENOV_ZMID_CLEAN_VECTOR_V1' "$serial"; then
    echo 'qemu-zvrt: corrupted protected payload was disclosed' >&2
    echo quit
    return 1
  fi

  verify_count="$(grep -Fc 'ZENOV_GUARD_AUDIT_VERIFY_OK' "$serial" || true)"
  send_command 'guard log verify'
  wait_for_count "$serial" 'ZENOV_GUARD_AUDIT_VERIFY_OK' $((verify_count + 1)) || { echo quit; return 1; }
  sync_count="$(grep -Fc 'SYNC_OK' "$serial" || true)"
  send_command 'sync'
  wait_for_count "$serial" 'SYNC_OK' $((sync_count + 1)) || { echo quit; return 1; }
  sleep 0.3
  echo quit
}

run_phase() {
  local controller="$1" serial="$2" monitor="$3" stderr="$4" data_image="$5"
  set +e
  "$controller" "$serial" | timeout 240s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$data_image,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?
  set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-zvrt: phase $controller failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    return 1
  fi
}

VALID_SERIAL="$OUT/serial-valid.log"
MANIFEST_SERIAL="$OUT/serial-manifest-corrupt.log"
DATA_SERIAL="$OUT/serial-data-corrupt.log"

run_phase controller_valid "$VALID_SERIAL" "$OUT/monitor-valid.log" "$OUT/qemu-valid.stderr" "$OUT/valid-runtime.img"
run_phase controller_manifest_corrupt "$MANIFEST_SERIAL" "$OUT/monitor-manifest-corrupt.log" "$OUT/qemu-manifest-corrupt.stderr" "$OUT/manifest-corrupt-runtime.img"
run_phase controller_data_corrupt "$DATA_SERIAL" "$OUT/monitor-data-corrupt.log" "$OUT/qemu-data-corrupt.stderr" "$OUT/data-corrupt-runtime.img"

cat "$VALID_SERIAL" "$MANIFEST_SERIAL" "$DATA_SERIAL" > "$OUT/serial.log"

for marker in \
  'ZVRT_ROOT_KEY_OK id=d28215ec62269ffc' \
  'ZVRT_PSS_SIGNATURE_OK' \
  'ZVRT_WORKSPACE_OK address=0x00310000 bytes=4096 supervisor-only=yes' \
  'ZVRT_MANIFEST_OK version=1 records=4 chunk=4096 leaves=5' \
  'ZVRT_READ_OK path=/apps/fileio.elf chunks=2' \
  'ZVRT_READ_OK path=/samples/clean.txt chunks=1' \
  'ZVRT_INIT_FAILED reason=payload-digest' \
  'Signed verified-read manifest validation failed.' \
  'ZVRT_READ_BLOCKED path=/samples/clean.txt reason=file-digest'; do
  grep -Fq "$marker" "$OUT/serial.log" || { echo "qemu-zvrt: missing marker: $marker" >&2; exit 1; }
done

! grep -Fq "$UI_MARKER" "$MANIFEST_SERIAL" || { echo 'qemu-zvrt: manifest corruption was not fail-closed' >&2; exit 1; }
! grep -Fq 'ZENOV_ZMID_CLEAN_VECTOR_V1' "$DATA_SERIAL" || { echo 'qemu-zvrt: data corruption disclosed payload' >&2; exit 1; }
test ! -s "$OUT/qemu-valid.stderr"
test ! -s "$OUT/qemu-manifest-corrupt.stderr"
test ! -s "$OUT/qemu-data-corrupt.stderr"

printf 'ZENOV_ZVRT_QEMU_OK valid=yes manifest_fail_closed=yes data_blocked=yes payload_disclosure=no multichunk=2 audit=durable\n'
