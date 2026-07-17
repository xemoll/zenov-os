#!/usr/bin/env bash
set -euo pipefail
QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-zenpkg}"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial-*.log "$OUT"/monitor-*.log "$OUT"/qemu-*.stderr

wait_for() {
  local file="$1" marker="$2"
  for _ in $(seq 1 500); do
    [[ -f "$file" ]] && grep -q "$marker" "$file" && return 0
    sleep 0.1
  done
  echo "zenpkg-qemu: missing marker: $marker" >&2
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
      '.') echo "sendkey dot 8" ;;
      '-') echo "sendkey minus 8" ;;
      '_') echo "sendkey shift-minus 8" ;;
      '/') echo "sendkey slash 8" ;;
      *) echo "zenpkg-qemu: unsupported key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}
send_command() { send_text "$1"; echo "sendkey ret 10"; }
wait_boot() {
  local serial="$1"
  wait_for "$serial" "ZENOVOS_BOOT_OK" && wait_for "$serial" "ZGDB_READY" \
    && wait_for "$serial" "ZENPKG_CATALOG_PSS_SIGNATURE_OK" && wait_for "$serial" "ZENPKG_MANAGER_READY" \
    && wait_for "$serial" "ZENOVOS_UI_READY" && wait_for "$serial" "$PROMPT"
}
phase_one() {
  local serial="$1"
  wait_boot "$serial" || { echo quit; return 1; }
  send_command "pkg status"; wait_for "$serial" "ZENPKG_STATUS_OK" || { echo quit; return 1; }
  send_command "pkg verify /data/packages/hello-native-0.1.0.zpk"; wait_for "$serial" "ZENPKG_VERIFY_OK" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.1.0.zpk"; wait_for "$serial" "ZENPKG_INSTALL_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg run hello-native"; wait_for "$serial" "ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0" || { echo quit; return 1; }; wait_for "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.2.0.zpk"; wait_for "$serial" "ZENPKG_CATALOG_REJECTED" || { echo quit; return 1; }
  send_command "pkg catalog update /data/security/updates/zenpkg-tampered.zpc"; wait_for "$serial" "ZENPKG_CATALOG_TAMPER_REJECTED" || { echo quit; return 1; }
  send_command "pkg catalog update /data/security/updates/zenpkg-v2.zpc"; wait_for "$serial" "ZENPKG_CATALOG_UPDATE_OK version=2" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.2.0.zpk"; wait_for "$serial" "ZENPKG_UPGRADE_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg rollback hello-native"; wait_for "$serial" "ZENPKG_ROLLBACK_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for "$serial" "ZENPKG_INFO name=hello-native version=0.1.0" || { echo quit; return 1; }
  sleep 0.2; echo quit
}
phase_two() {
  local serial="$1"
  wait_boot "$serial" || { echo quit; return 1; }
  wait_for "$serial" "ZENPKG_DB_RECOVERED" || { echo quit; return 1; }
  wait_for "$serial" "ZENPKG_CATALOG_VERSION_OK version=2" || { echo quit; return 1; }
  send_command "pkg run hello-native"; wait_for "$serial" "ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0" || { echo quit; return 1; }
  send_command "pkg rollback hello-native"; wait_for "$serial" "ZENPKG_ROLLBACK_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg run hello-native"; wait_for "$serial" "ZENPKG_EXEC_ALLOWED name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg remove hello-native"; wait_for "$serial" "ZENPKG_REMOVE_COMMIT_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}
phase_three() {
  local serial="$1"
  wait_boot "$serial" || { echo quit; return 1; }
  wait_for "$serial" "ZENPKG_DB_RECOVERED" || { echo quit; return 1; }
  send_command "pkg list"; wait_for "$serial" "ZENPKG_LIST_OK" || { echo quit; return 1; }
  send_command "pkg run hello-native"; wait_for "$serial" "ZENPKG_NOT_INSTALLED" || { echo quit; return 1; }
  send_command "fsck"; wait_for "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}
run_phase() {
  local controller="$1" serial="$2" monitor="$3" stderr="$4"
  set +e
  "$controller" "$serial" | timeout 65s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$DATA_IMAGE,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?; set -e
  if [[ $status -ne 0 ]]; then
    echo "zenpkg-qemu: phase failed: $status" >&2
    cat "$monitor" "$stderr" "$serial" >&2 || true
    return 1
  fi
}
SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
SERIAL3="$(cd "$OUT" && pwd)/serial-phase3.log"
run_phase phase_one "$SERIAL1" "$OUT/monitor-phase1.log" "$OUT/qemu-phase1.stderr"
run_phase phase_two "$SERIAL2" "$OUT/monitor-phase2.log" "$OUT/qemu-phase2.stderr"
run_phase phase_three "$SERIAL3" "$OUT/monitor-phase3.log" "$OUT/qemu-phase3.stderr"
cat "$SERIAL1" "$SERIAL2" "$SERIAL3" > "$OUT/serial.log"
for marker in ZENPKG_CATALOG_PSS_SIGNATURE_OK ZENPKG_MANAGER_READY ZENPKG_VERIFY_OK ZENPKG_INSTALL_COMMIT_OK \
  ZENPKG_CATALOG_REJECTED ZENPKG_CATALOG_TAMPER_REJECTED "ZENPKG_CATALOG_UPDATE_OK version=2" \
  ZENPKG_UPGRADE_COMMIT_OK ZENPKG_ROLLBACK_COMMIT_OK ZENPKG_REMOVE_COMMIT_OK ZENPKG_DB_RECOVERED \
  "ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0" "ZENPKG_EXEC_ALLOWED name=hello-native version=0.2.0" \
  HELLO_ZEX_0_1_1_OK ZENOVFS_FSCK_OK; do
  grep -q "$marker" "$OUT/serial.log" || { echo "zenpkg-qemu: final marker missing: $marker" >&2; exit 1; }
done
printf 'zenpkg-qemu: OK signed-catalog install upgrade rollback final-read persistence removal serial=%s\n' "$OUT/serial.log"
