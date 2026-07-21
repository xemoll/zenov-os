#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
INVALID_JOURNAL_IMAGE="${2:-build/qemu/zenpkg-fault-invalid-journal.img}"
COMMIT_RECOVERY_IMAGE="${3:-build/qemu/zenpkg-fault-commit-recovery.img}"
CORRUPT_FINAL_IMAGE="${4:-build/qemu/zenpkg-fault-corrupt-final.img}"
OUT="${5:-build/qemu/zenpkg-faults}"
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
      *) echo "qemu-zenpkg-transport-faults: unsupported key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() { send_text "$1"; echo "sendkey ret 10"; }

wait_for_base_boot() {
  local serial="$1"
  wait_for_serial "$serial" "ZENOVOS_BOOT_OK" \
    && wait_for_serial "$serial" "ZENOVFS_MOUNT_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_READY" \
    && wait_for_serial "$serial" "ZENREPO_READY trust=verified packages=2" \
    && wait_for_serial "$serial" "ZENPKG_SHA256_OK" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_invalid_journal() {
  local serial="$1"
  wait_for_base_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_JOURNAL_INVALID_RESET" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=0 partials=1" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_MANAGER_READY" || { echo quit; return 1; }
  send_command "pkg transport status"
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS_IDLE" || { echo quit; return 1; }
  send_command "pkg cache status"
  wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0 partial=1" || { echo quit; return 1; }
  send_command "pkg transport resume hello-native"
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_ADOPTED_PARTIAL name=hello-native version=0.2.0 offset=512 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_COMPLETE name=hello-native version=0.2.0 offset=647 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg cache verify"
  wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=1 partials=0" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_commit_recovery() {
  local serial="$1"
  wait_for_base_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_COMMIT_RECOVERED name=hello-native version=0.2.0" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=1 partials=0" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_MANAGER_READY" || { echo quit; return 1; }
  send_command "pkg transport status"
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_STATUS_IDLE" || { echo quit; return 1; }
  send_command "pkg cache status"
  wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=1 invalid=0 partial=0" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_corrupt_final() {
  local serial="$1"
  wait_for_base_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_TRANSPORT_JOURNAL_RECONCILED name=hello-native version=0.2.0 offset=0 expected=647" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_REJECTED" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_CACHE_INIT_REJECTED" || { echo quit; return 1; }
  sleep 0.5
  if grep -Fq "ZENPKG_MANAGER_READY" "$serial"; then
    echo "qemu-zenpkg-transport-faults: corrupt final reached manager ready" >&2
    echo quit
    return 1
  fi
  echo quit
}

run_phase() {
  local controller="$1" name="$2" image="$3"
  local serial="$OUT/serial-$name.log" monitor="$OUT/monitor-$name.log" stderr="$OUT/qemu-$name.stderr"
  set +e
  "$controller" "$serial" | timeout 80s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$image,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?; set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-zenpkg-transport-faults: phase $name failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    return 1
  fi
  [[ ! -s "$stderr" ]] || { cat "$stderr" >&2; return 1; }
}

for image in "$BOOT_IMAGE" "$INVALID_JOURNAL_IMAGE" "$COMMIT_RECOVERY_IMAGE" "$CORRUPT_FINAL_IMAGE"; do
  [[ -f "$image" ]] || { echo "qemu-zenpkg-transport-faults: missing image: $image" >&2; exit 1; }
done

run_phase controller_invalid_journal invalid-journal "$INVALID_JOURNAL_IMAGE"
run_phase controller_commit_recovery commit-recovery "$COMMIT_RECOVERY_IMAGE"
run_phase controller_corrupt_final corrupt-final "$CORRUPT_FINAL_IMAGE"
cat "$OUT"/serial-invalid-journal.log "$OUT"/serial-commit-recovery.log "$OUT"/serial-corrupt-final.log > "$OUT/serial.log"

for marker in \
  ZENPKG_TRANSPORT_JOURNAL_INVALID_RESET \
  'ZENPKG_CACHE_VERIFY_OK objects=0 partials=1' \
  'ZENPKG_TRANSPORT_ADOPTED_PARTIAL name=hello-native version=0.2.0 offset=512 expected=647' \
  'ZENPKG_TRANSPORT_COMPLETE name=hello-native version=0.2.0 offset=647 expected=647' \
  'ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0' \
  'ZENPKG_TRANSPORT_COMMIT_RECOVERED name=hello-native version=0.2.0' \
  'ZENPKG_CACHE_STATUS_OK valid=1 invalid=0 partial=0' \
  'ZENPKG_TRANSPORT_JOURNAL_RECONCILED name=hello-native version=0.2.0 offset=0 expected=647' \
  ZENPKG_CACHE_VERIFY_REJECTED ZENPKG_CACHE_INIT_REJECTED ZENOVFS_FSCK_OK; do
  grep -Fq "$marker" "$OUT/serial.log" || { echo "qemu-zenpkg-transport-faults: missing marker: $marker" >&2; exit 1; }
done

[[ "$(grep -Fxc 'ZENPKG_CACHE_INIT_REJECTED' "$OUT/serial-corrupt-final.log")" -eq 1 ]] || {
  echo "qemu-zenpkg-transport-faults: corrupt final did not fail closed exactly once" >&2; exit 1;
}
! grep -Eq 'PANIC|ASSERT|DOUBLE FAULT|ZENPKG_TRANSPORT_RETRY_EXHAUSTED' "$OUT/serial.log"
printf 'qemu-zenpkg-transport-faults: OK invalid-journal-reset+partial-adoption+commit-recovery+corrupt-final-fail-closed serial=%s\n' "$OUT/serial.log"
