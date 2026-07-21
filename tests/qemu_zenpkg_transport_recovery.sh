#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/qemu/zenpkg-transport-runtime.img}"
OUT="${3:-build/qemu/zenpkg-transport}"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial-*.log "$OUT"/monitor-*.log "$OUT"/qemu-*.stderr "$OUT"/serial.log

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 600); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
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
      *) echo "qemu-zenpkg-transport: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() { send_text "$1"; echo "sendkey ret 10"; }

wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" "ZENOVOS_BOOT_OK" \
    && wait_for_serial "$serial" "ZENOVFS_MOUNT_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_READY" \
    && wait_for_serial "$serial" "SYSCALL_CAPABILITY_POLICY_OK" \
    && wait_for_serial "$serial" "ZENREPO_READY trust=verified packages=2" \
    && wait_for_serial "$serial" "ZENPKG_SHA256_OK" \
    && wait_for_serial "$serial" "ZENPKG_CACHE_READY" \
    && wait_for_serial "$serial" "ZENPKG_MANAGER_READY" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_pause_after_one_chunk() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "pkg transport status"; wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS_IDLE" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0 partial=0" || { echo quit; return 1; }
  send_command "pkg transport step hello-native"
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_BEGIN name=hello-native version=0.2.0 offset=0 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_CHUNK_COMMIT name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_PAUSED name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "PKG_TRANSPORT_STEP_PENDING" || { echo quit; return 1; }
  send_command "pkg transport status"; wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS phase=downloading name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0 partial=1" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_resume_after_reboot() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_JOURNAL_RECOVERED name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  send_command "pkg transport status"; wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS phase=downloading name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  send_command "pkg transport resume hello-native"
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_RESUME name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_CHUNK_COMMIT name=hello-native version=0.2.0 offset=647 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_COMPLETE name=hello-native version=0.2.0 offset=647 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_JOURNAL_CLEARED name=hello-native version=0.2.0" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0" || { echo quit; return 1; }
  wait_for_serial "$serial" "PKG_TRANSPORT_RESUME_OK" || { echo quit; return 1; }
  send_command "pkg transport status"; wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS_IDLE" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=1 invalid=0 partial=0" || { echo quit; return 1; }
  send_command "pkg cache verify"; wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=1 partials=0" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  send_command "pkg cache clean"; wait_for_serial "$serial" "ZENPKG_CACHE_CLEAN_OK removed=1" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0 partial=0" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_final_clean_boot() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "pkg transport status"; wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS_IDLE" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0 partial=0" || { echo quit; return 1; }
  send_command "pkg cache verify"; wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=0 partials=0" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

run_phase() {
  local controller="$1" name="$2"
  local serial="$OUT/serial-$name.log" monitor="$OUT/monitor-$name.log" stderr="$OUT/qemu-$name.stderr"
  set +e
  "$controller" "$serial" | timeout 80s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$DATA_IMAGE,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?; set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-zenpkg-transport: phase $name failed with status $status" >&2
    cat "$monitor" >&2 || true; cat "$stderr" >&2 || true; cat "$serial" >&2 || true
    return 1
  fi
  [[ ! -s "$stderr" ]] || { echo "qemu-zenpkg-transport: non-empty stderr in $name" >&2; cat "$stderr" >&2; return 1; }
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" ]] || { echo "qemu-zenpkg-transport: boot and data images are required" >&2; exit 1; }
run_phase controller_pause_after_one_chunk pause
run_phase controller_resume_after_reboot resume
run_phase controller_final_clean_boot final
cat "$OUT"/serial-pause.log "$OUT"/serial-resume.log "$OUT"/serial-final.log > "$OUT/serial.log"

for marker in \
  'ZENPKG_TRANSPORT_CHUNK_COMMIT name=hello-native version=0.2.0 offset=512 expected=647' \
  'ZENPKG_TRANSPORT_PAUSED name=hello-native version=0.2.0 offset=512 expected=647' \
  'ZENPKG_TRANSPORT_JOURNAL_RECOVERED name=hello-native version=0.2.0 offset=512 expected=647' \
  'ZENPKG_TRANSPORT_RESUME name=hello-native version=0.2.0 offset=512 expected=647' \
  'ZENPKG_TRANSPORT_CHUNK_COMMIT name=hello-native version=0.2.0 offset=647 expected=647' \
  'ZENPKG_TRANSPORT_COMPLETE name=hello-native version=0.2.0 offset=647 expected=647' \
  'ZENPKG_TRANSPORT_JOURNAL_CLEARED name=hello-native version=0.2.0' \
  'ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0' \
  'ZENPKG_CACHE_VERIFY_OK objects=1 partials=0' \
  'ZENPKG_CACHE_VERIFY_OK objects=0 partials=0' ZENOVFS_FSCK_OK; do
  grep -Fq "$marker" "$OUT/serial.log" || { echo "qemu-zenpkg-transport: missing marker: $marker" >&2; exit 1; }
done

! grep -Fq 'ZENPKG_TRANSPORT_RETRY_EXHAUSTED' "$OUT/serial.log"
! grep -Fq 'ZENPKG_TRANSPORT_PARTIAL_RESET' "$OUT/serial.log"
! grep -Fq 'ZENPKG_TRANSPORT_JOURNAL_INVALID_RESET' "$OUT/serial.log"
! grep -Fq 'ZENPKG_TRANSPORT_BUSY' "$OUT/serial.log"
[[ "$(grep -Fc 'ZENPKG_TRANSPORT_CHUNK_COMMIT name=hello-native version=0.2.0' "$OUT/serial.log")" -eq 2 ]] || {
  echo "qemu-zenpkg-transport: expected exactly two durable chunks" >&2; exit 1;
}
[[ "$(grep -Fc ZENPKG_MANAGER_READY "$OUT/serial.log")" -eq 3 ]] || {
  echo "qemu-zenpkg-transport: manager did not recover on all three boots" >&2; exit 1;
}
printf 'qemu-zenpkg-transport: OK persistent-journal+one-chunk-pause+reboot-resume+verified-commit+cleanup serial=%s\n' "$OUT/serial.log"
