#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/qemu/zenpkg-runtime.img}"
OUT="${3:-build/qemu/zenpkg}"
PROMPT="zenov> "
mkdir -p "$OUT"
rm -f "$OUT"/serial-*.log "$OUT"/monitor-*.log "$OUT"/qemu-*.stderr "$OUT"/serial.log

wait_for_serial() {
  local file="$1" text="$2"
  for _ in $(seq 1 600); do
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
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower 10" ;;
      ' ') echo "sendkey spc 10" ;;
      '.') echo "sendkey dot 10" ;;
      '-') echo "sendkey minus 10" ;;
      '_') echo "sendkey shift-minus 10" ;;
      '/') echo "sendkey slash 10" ;;
      *) echo "qemu-zenpkg: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() { send_text "$1"; echo "sendkey ret 10"; }

wait_for_boot() {
  local serial="$1"
  wait_for_serial "$serial" "ZENOVOS_BOOT_OK" \
    && wait_for_serial "$serial" "ZENOVFS_MOUNT_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_READY" \
    && wait_for_serial "$serial" "ZENOV_GUARD_READY" \
    && wait_for_serial "$serial" "ZGDB_POLICY_VERSION_OK version=3" \
    && wait_for_serial "$serial" "ZGDB_READY" \
    && wait_for_serial "$serial" "SYSCALL_CAPABILITY_POLICY_OK" \
    && wait_for_serial "$serial" "ZENREPO_READY trust=verified packages=2" \
    && wait_for_serial "$serial" "ZENREPO_PROTECTED_PATH_TEST_OK" \
    && wait_for_serial "$serial" "ZENPKG_SHA256_OK" \
    && wait_for_serial "$serial" "ZENPKG_PROTECTED_PATH_TEST_OK" \
    && wait_for_serial "$serial" "ZENPKG_CATALOG_READY entries=2" \
    && wait_for_serial "$serial" "ZENPKG_CACHE_READY" \
    && wait_for_serial "$serial" "ZENPKG_MANAGER_READY" \
    && wait_for_serial "$serial" "ZENOVOS_UI_READY" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_install_upgrade_rollback() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  send_command "pkg repo status"; wait_for_serial "$serial" "ZENREPO_STATUS_OK" || { echo quit; return 1; }
  send_command "pkg repo targets"; wait_for_serial "$serial" "ZENREPO_TARGETS_OK" || { echo quit; return 1; }
  send_command "pkg repo check"; wait_for_serial "$serial" "PKG_REPOSITORY_CHECK_OK" || { echo quit; return 1; }
  send_command "pkg repo refresh"; wait_for_serial "$serial" "PKG_REPOSITORY_REFRESH_OK" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0" || { echo quit; return 1; }
  send_command "pkg search hello"; wait_for_serial "$serial" "ZENPKG_SEARCH_OK" || { echo quit; return 1; }
  send_command "pkg plan hello-native"; wait_for_serial "$serial" "ZENPKG_PLAN_INSTALL" || { echo quit; return 1; }
  send_command "pkg policy hello-native"; wait_for_serial "$serial" "ZENREPO_POLICY_OK" || { echo quit; return 1; }
  send_command "pkg status"; wait_for_serial "$serial" "ZENPKG_STATUS_OK" || { echo quit; return 1; }
  send_command "pkg verify /data/packages/hello-native-0.3.0.zpk"; wait_for_serial "$serial" "ZENPKG_VERIFY_UNAUTHORIZED" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.3.0.zpk"; wait_for_serial "$serial" "ZENPKG_UNAUTHORIZED_PACKAGE" || { echo quit; return 1; }
  send_command "pkg verify /data/packages/hello-native-0.1.0.zpk"; wait_for_serial "$serial" "ZENPKG_VERIFY_AUTHORIZED" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.1.0.zpk"; wait_for_serial "$serial" "ZENPKG_INSTALL_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.1.0.zpk"; wait_for_serial "$serial" "ZENPKG_INSTALL_IDEMPOTENT_OK" || { echo quit; return 1; }
  send_command "pkg plan hello-native"; wait_for_serial "$serial" "ZENPKG_PLAN_UPGRADE" || { echo quit; return 1; }
  send_command "pkg upgrade hello-native"; wait_for_serial "$serial" "ZENPKG_UPGRADE_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg install /data/packages/hello-native-0.1.0.zpk"; wait_for_serial "$serial" "ZENPKG_DOWNGRADE_REJECTED" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_INFO name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg rollback hello-native"; wait_for_serial "$serial" "ZENPKG_ROLLBACK_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_INFO name=hello-native version=0.1.0" || { echo quit; return 1; }
  send_command "pkg repair hello-native"; wait_for_serial "$serial" "ZENPKG_REPAIR_HEALTHY" || { echo quit; return 1; }
  send_command "pkg run hello-native"; \
    wait_for_serial "$serial" "ZENPKG_RUN name=hello-native version=0.1.0" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_SYSCALL_PROFILE_ACTIVE app=/apps/pkg-hello-native-0.1.0.zex mask=0x00000001" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_persist_and_remove() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_DB_RECOVERED" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_INFO name=hello-native version=0.1.0" || { echo quit; return 1; }
  send_command "pkg run hello-native"; wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  send_command "pkg remove hello-native"; wait_for_serial "$serial" "ZENPKG_REMOVE_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_removed_then_repository_install() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_DB_RECOVERED" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_NOT_INSTALLED" || { echo quit; return 1; }
  send_command "pkg fetch hello-native"; \
    wait_for_serial "$serial" "ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=1 invalid=0" || { echo quit; return 1; }
  send_command "pkg cache verify"; wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=1" || { echo quit; return 1; }
  send_command "pkg install hello-native"; \
    wait_for_serial "$serial" "ZENPKG_CACHE_HIT name=hello-native version=0.2.0" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_CACHE_SELECTED name=hello-native version=0.2.0" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_REPOSITORY_TARGET_RESOLVED name=hello-native version=0.2.0 path=/var/cache/zp/" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_REPOSITORY_INSTALL_OK name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_INFO name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg run hello-native"; \
    wait_for_serial "$serial" "ZENPKG_RUN name=hello-native version=0.2.0" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_EXEC_ALLOWED name=hello-native version=0.2.0" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "ZENPKG_SYSCALL_PROFILE_ACTIVE app=/apps/pkg-hello-native-0.2.0.zex mask=0x00000001" || { echo quit; return 1; }; \
    wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_repository_install_persists_and_remove() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_DB_RECOVERED" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=1 invalid=0" || { echo quit; return 1; }
  send_command "pkg cache verify"; wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=1" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_INFO name=hello-native version=0.2.0" || { echo quit; return 1; }
  send_command "pkg run hello-native"; wait_for_serial "$serial" "HELLO_ZEX_0_1_1_OK" || { echo quit; return 1; }
  send_command "pkg remove hello-native"; wait_for_serial "$serial" "ZENPKG_REMOVE_COMMIT_OK" || { echo quit; return 1; }
  send_command "pkg cache clean"; wait_for_serial "$serial" "ZENPKG_CACHE_CLEAN_OK removed=1" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_final_removed_persists() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENPKG_DB_RECOVERED" || { echo quit; return 1; }
  send_command "pkg info hello-native"; wait_for_serial "$serial" "ZENPKG_NOT_INSTALLED" || { echo quit; return 1; }
  send_command "pkg list"; wait_for_serial "$serial" "ZENPKG_LIST_OK" || { echo quit; return 1; }
  send_command "pkg cache status"; wait_for_serial "$serial" "ZENPKG_CACHE_STATUS_OK valid=0 invalid=0" || { echo quit; return 1; }
  send_command "pkg cache verify"; wait_for_serial "$serial" "ZENPKG_CACHE_VERIFY_OK objects=0" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" || { echo quit; return 1; }
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
    echo "qemu-zenpkg: phase $name failed with status $status" >&2
    cat "$monitor" >&2 || true; cat "$stderr" >&2 || true; cat "$serial" >&2 || true
    return 1
  fi
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" ]] || { echo "qemu-zenpkg: boot and data images are required" >&2; exit 1; }
run_phase controller_install_upgrade_rollback install
run_phase controller_persist_and_remove remove
run_phase controller_removed_then_repository_install repository-install
run_phase controller_repository_install_persists_and_remove repository-remove
run_phase controller_final_removed_persists final-removed
cat "$OUT"/serial-install.log "$OUT"/serial-remove.log "$OUT"/serial-repository-install.log "$OUT"/serial-repository-remove.log "$OUT"/serial-final-removed.log > "$OUT/serial.log"

for marker in \
  'ZENREPO_READY trust=verified packages=2' ZENREPO_PROTECTED_PATH_TEST_OK ZENREPO_STATUS_OK ZENREPO_TARGETS_OK PKG_REPOSITORY_CHECK_OK PKG_REPOSITORY_REFRESH_OK ZENPKG_SEARCH_OK ZENPKG_PLAN_INSTALL ZENREPO_POLICY_OK \
  ZENPKG_SHA256_OK ZENPKG_PROTECTED_PATH_TEST_OK 'ZENPKG_CATALOG_READY entries=2' ZENPKG_CACHE_READY ZENPKG_DB_CREATED ZENPKG_DB_RECOVERED ZENPKG_MANAGER_READY ZENPKG_STATUS_OK \
  ZENPKG_VERIFY_UNAUTHORIZED ZENPKG_UNAUTHORIZED_PACKAGE ZENPKG_VERIFY_AUTHORIZED ZENPKG_INSTALL_COMMIT_OK ZENPKG_INSTALL_IDEMPOTENT_OK \
  ZENPKG_PLAN_UPGRADE ZENPKG_UPGRADE_COMMIT_OK ZENPKG_DOWNGRADE_REJECTED ZENPKG_REPAIR_HEALTHY ZENPKG_ROLLBACK_COMMIT_OK \
  'ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0' 'ZENPKG_CACHE_HIT name=hello-native version=0.2.0' \
  'ZENPKG_CACHE_SELECTED name=hello-native version=0.2.0' 'ZENPKG_CACHE_STATUS_OK valid=1 invalid=0' \
  'ZENPKG_CACHE_STATUS_OK valid=0 invalid=0' 'ZENPKG_CACHE_VERIFY_OK objects=1' 'ZENPKG_CACHE_VERIFY_OK objects=0' \
  'ZENPKG_CACHE_CLEAN_OK removed=1' \
  'ZENPKG_REPOSITORY_TARGET_RESOLVED name=hello-native version=0.2.0 path=/var/cache/zp/' \
  'ZENPKG_REPOSITORY_INSTALL_OK name=hello-native version=0.2.0' \
  'ZENPKG_INFO name=hello-native version=0.2.0' 'ZENPKG_INFO name=hello-native version=0.1.0' \
  'ZENPKG_RUN name=hello-native version=0.1.0' 'ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0' \
  'ZENPKG_RUN name=hello-native version=0.2.0' 'ZENPKG_EXEC_ALLOWED name=hello-native version=0.2.0' \
  'ZENPKG_SYSCALL_PROFILE_ACTIVE app=/apps/pkg-hello-native-0.1.0.zex mask=0x00000001' \
  'ZENPKG_SYSCALL_PROFILE_ACTIVE app=/apps/pkg-hello-native-0.2.0.zex mask=0x00000001' HELLO_ZEX_0_1_1_OK \
  ZENPKG_REMOVE_COMMIT_OK ZENPKG_NOT_INSTALLED ZENPKG_LIST_OK ZENOV_GUARD_AUDIT_VERIFY_OK ZENOVFS_FSCK_OK; do
  grep -q "$marker" "$OUT/serial.log" || { echo "qemu-zenpkg: missing marker: $marker" >&2; exit 1; }
done
[[ "$(grep -c ZENREPO_READY "$OUT/serial.log")" -ge 6 ]] || { echo "qemu-zenpkg: repository did not initialize and refresh" >&2; exit 1; }
[[ "$(grep -c ZENPKG_CACHE_READY "$OUT/serial.log")" -eq 5 ]] || { echo "qemu-zenpkg: cache did not initialize on every boot" >&2; exit 1; }
[[ "$(grep -c ZENPKG_MANAGER_READY "$OUT/serial.log")" -eq 5 ]] || { echo "qemu-zenpkg: manager did not initialize on every boot" >&2; exit 1; }
[[ "$(grep -c ZENPKG_DB_RECOVERED "$OUT/serial.log")" -ge 4 ]] || { echo "qemu-zenpkg: database did not persist across reboots" >&2; exit 1; }
[[ "$(grep -c HELLO_ZEX_0_1_1_OK "$OUT/serial.log")" -ge 4 ]] || { echo "qemu-zenpkg: installed app did not run across local and cached repository installs" >&2; exit 1; }
[[ "$(grep -c 'ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0' "$OUT/serial.log")" -eq 1 ]] || { echo "qemu-zenpkg: cache fetch did not commit exactly once" >&2; exit 1; }
[[ "$(grep -c 'ZENPKG_REPOSITORY_INSTALL_OK name=hello-native version=0.2.0' "$OUT/serial.log")" -eq 1 ]] || { echo "qemu-zenpkg: repository install did not complete exactly once" >&2; exit 1; }
printf 'qemu-zenpkg: OK signed-offline-repo+verified-cache+atomic-fetch+local-install+upgrade+downgrade-block+rollback+repository-install+least-privilege+audit+persistence+cache-clean+remove serial=%s\n' "$OUT/serial.log"
