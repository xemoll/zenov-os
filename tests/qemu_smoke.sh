#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu}"
RECOVERY_IMAGE="${4:-}"
AUDIT_OLD_RECOVERY_IMAGE="${5:-}"
AUDIT_NEW_RECOVERY_IMAGE="${6:-}"
AUDIT_CORRUPT_IMAGE="${7:-}"
ZCAP_CORRUPT_IMAGE="${8:-}"
ZMID_CORRUPT_IMAGE="${9:-}"
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
  for _ in $(seq 1 500); do
    [[ -f "$file" ]] && grep -q "$text" "$file" && return 0
    sleep 0.1
  done
  return 1
}
wait_for_count() {
  local file="$1" text="$2" expected="$3"
  for _ in $(seq 1 500); do
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
    && wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_SELFTEST_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_REPLAY_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_READY" \
    && wait_for_serial "$serial" "ZENOV_GUARD_SELFTEST_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_TRUST_BASELINE_OK" \
    && wait_for_serial "$serial" "ZENOV_GUARD_READY" \
    && wait_for_serial "$serial" "ZGDB_ROOT_KEY_OK id=6f788074c018f5aa" \
    && wait_for_serial "$serial" "ZGDB_PSS_SIGNATURE_OK" \
    && wait_for_serial "$serial" "ZGDB_READY" \
    && wait_for_serial "$serial" "ZCAP_ROOT_KEY_OK id=9202c73fad96ad66" \
    && wait_for_serial "$serial" "ZCAP_PSS_SIGNATURE_OK" \
    && wait_for_serial "$serial" "ZCAP_READY" \
    && wait_for_serial "$serial" "ZMID_ROOT_KEY_OK id=6ca6a5275544c533" \
    && wait_for_serial "$serial" "ZMID_PSS_SIGNATURE_OK" \
    && wait_for_serial "$serial" "ZMID_READY" \
    && wait_for_serial "$serial" "SYSCALL_CAPABILITY_POLICY_OK" \
    && wait_for_serial "$serial" "SYSCALL_CAPABILITY_PROFILES_OK count=7" \
    && wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" \
    && wait_for_serial "$serial" "GRAPHICS_PCI_OK" \
    && wait_for_serial "$serial" "FRAMEBUFFER_MAPPED_OK" \
    && wait_for_serial "$serial" "GRAPHICS_MODE_OK" \
    && wait_for_serial "$serial" "BACKBUFFER_PRESENT_OK" \
    && wait_for_serial "$serial" "CLIPPING_OK" \
    && wait_for_serial "$serial" "ALPHA_BLEND_OK" \
    && wait_for_serial "$serial" "FONT_RENDER_OK" \
    && wait_for_serial "$serial" "DESKTOP_SCENE_OK" \
    && wait_for_serial "$serial" "GRAPHICAL_DESKTOP_READY" \
    && wait_for_serial "$serial" "PS2_MOUSE_OK" \
    && wait_for_serial "$serial" "PS2_MOUSE_IRQ_ROUTE_OK" \
    && wait_for_serial "$serial" "MOUSE_PACKET_OK" \
    && wait_for_serial "$serial" "WINDOW_DRAG_OK" \
    && wait_for_serial "$serial" "PS2_MOUSE_DECODER_OK" \
    && wait_for_serial "$serial" "$UI_MARKER" \
    && wait_for_serial "$serial" "$PROMPT"
}

controller_first() {
  local serial="$1" prompt_count=1
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZGDB_POLICY_VERSION_OK version=3" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZCAP_POLICY_VERSION_OK version=1" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZMID_DATABASE_VERSION_OK version=1" || { echo quit; return 1; }
  sleep 0.3; echo "screendump $SCREENSHOT"; sleep 0.2

  send_command "guard status"; wait_for_serial "$serial" "ZENOV_GUARD_STATUS_OK" || { echo quit; return 1; }
  send_command "guard intelligence"; wait_for_serial "$serial" "ZMID_STATUS_OK" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 2 || { echo quit; return 1; }
  send_command "guard selftest"; wait_for_count "$serial" "ZENOV_GUARD_SELFTEST_OK" 2 || { echo quit; return 1; }
  send_command "write /data/ransomware-write.bin ZENOV_RANSOMWARE_TEST_V1"; wait_for_serial "$serial" "ZENOV_GUARD_WRITE_BLOCKED path=/ransomware-write.bin" || { echo quit; return 1; }
  send_command "write /data/pua-audit.bin ZENOV_PUA_TEST_V1"; wait_for_serial "$serial" "ZENOV_GUARD_WRITE_AUDIT path=/pua-audit.bin" || { echo quit; return 1; }; wait_for_serial "$serial" "WRITE_OK" || { echo quit; return 1; }
  send_command "write /data/split.bin prefix-ZENOV_RANSOMWARE_"; wait_for_count "$serial" "WRITE_OK" 2 || { echo quit; return 1; }
  send_command "append /data/split.bin TEST_V1-suffix"; wait_for_serial "$serial" "ZENOV_GUARD_WRITE_BLOCKED path=/split.bin" || { echo quit; return 1; }
  send_command "guard scan /data/samples/ransomware-test.bin"; wait_for_serial "$serial" "ZENOV_GUARD_DETECTED" || { echo quit; return 1; }
  send_command "guard quarantine /data/samples/ransomware-test.bin"; wait_for_serial "$serial" "ZENOV_GUARD_QUARANTINE_OK" || { echo quit; return 1; }
  send_command "guard quarantine list"; wait_for_serial "$serial" "ZENOV_GUARD_QUARANTINE_LIST_OK entries=2" || { echo quit; return 1; }
  send_command "cp /data/apps/hello.zex /data/apps/untrusted.zex"; wait_for_serial "$serial" "COPY_OK" || { echo quit; return 1; }
  send_command "run UNTRUSTED.ZEX"; wait_for_serial "$serial" "ZENOV_GUARD_UNTRUSTED_BLOCKED" || { echo quit; return 1; }
  send_command "rm /data/apps/untrusted.zex"; wait_for_serial "$serial" "REMOVE_OK" || { echo quit; return 1; }
  send_command "guard scan all"; wait_for_serial "$serial" "ZENOV_GUARD_FULL_SCAN_OK" || { echo quit; return 1; }

  echo "sendkey f1 10"; wait_for_serial "$serial" "COMMAND REFERENCE" || { echo quit; return 1; }; echo "sendkey f4 10"; sleep 0.2
  send_command "vm"; wait_for_serial "$serial" "VIRTUAL MEMORY" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  local long_payload; long_payload="$(printf 'a%.0s' {1..160})${LONG_INPUT_MARKER}"
  send_command "echo $long_payload"; wait_for_serial "$serial" "$LONG_INPUT_MARKER" || { echo quit; return 1; }
  send_command "write PERSIST.TXT PERSISTENCE_0_1_1_OK"; wait_for_count "$serial" "WRITE_OK" 2 || { echo quit; return 1; }
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

  send_command "guard intelligence-update /security/updates/zmid-wrong-key.zmid"
  wait_for_serial "$serial" "ZMID_KEY_REJECTED reason=unknown-key" || { echo quit; return 1; }
  send_command "guard intelligence-update /security/updates/zmid-tampered.zmid"
  wait_for_serial "$serial" "ZMID_TAMPER_REJECTED reason=payload-digest" || { echo quit; return 1; }
  send_command "guard intelligence-update /security/updates/zmid-v2.zmid"
  wait_for_serial "$serial" "ZMID_ATOMIC_UPDATE_OK version=2" || { echo quit; return 1; }
  send_command "guard scan /data/samples/malware-v2.bin"
  wait_for_serial "$serial" "ZENOV_GUARD_DETECTED path=/samples/malware-v2.bin" || { echo quit; return 1; }
  send_command "guard intelligence-update /security/updates/zmid-v1.zmid"
  wait_for_serial "$serial" "ZMID_ROLLBACK_REJECTED reason=rollback" || { echo quit; return 1; }

  send_command "guard capability-update /security/updates/zcap-wrong-key.zcap"
  wait_for_serial "$serial" "ZCAP_KEY_REJECTED reason=unknown-key" || { echo quit; return 1; }
  send_command "guard capability-update /security/updates/zcap-tampered.zcap"
  wait_for_serial "$serial" "ZCAP_TAMPER_REJECTED" || { echo quit; return 1; }
  send_command "guard capability-update /security/updates/zcap-v2.zcap"
  wait_for_serial "$serial" "ZCAP_ATOMIC_UPDATE_OK version=2" || { echo quit; return 1; }
  prompt_count="$(grep -c "$PROMPT" "$serial" || true)"
  send_command "run HELLO"
  wait_for_serial "$serial" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000000" || { echo quit; return 1; }
  wait_for_serial "$serial" "SYSCALL_CAPABILITY_DENIED app=/apps/hello.zex syscall=1 capability=console-write reason=missing-capability" || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }
  send_command "guard capability-update /security/updates/zcap-v1.zcap"
  wait_for_serial "$serial" "ZCAP_ROLLBACK_REJECTED" || { echo quit; return 1; }

  send_command "guard update /security/updates/zenovguard-wrong-key.zgdb"
  wait_for_serial "$serial" "ZGDB_KEY_REJECTED reason=unknown-key" || { echo quit; return 1; }
  send_command "guard update /security/updates/zenovguard-tampered.zgdb"
  wait_for_serial "$serial" "ZGDB_TAMPER_REJECTED" || { echo quit; return 1; }
  send_command "guard update /security/updates/zenovguard-v4.zgdb"
  wait_for_serial "$serial" "ZGDB_ATOMIC_UPDATE_OK version=4" || { echo quit; return 1; }
  prompt_count="$(grep -c "$PROMPT" "$serial" || true)"
  send_command "run ZENOVAPP.ZEX"
  wait_for_serial "$serial" "ZGDB_REVOCATION_BLOCKED" || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }
  send_command "guard update /security/updates/zenovguard-v3.zgdb"
  wait_for_serial "$serial" "ZGDB_ROLLBACK_REJECTED" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 3 || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_second() {
  local serial="$1" prompt_count
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZGDB_POLICY_VERSION_OK version=4" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZCAP_POLICY_VERSION_OK version=2" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZMID_DATABASE_VERSION_OK version=2" || { echo quit; return 1; }
  grep -Eq 'ZENOV_GUARD_AUDIT_REPLAY_OK count=[1-9][0-9]*' "$serial" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 2 || { echo quit; return 1; }
  prompt_count="$(grep -c "$PROMPT" "$serial" || true)"
  send_command "run HELLO"
  wait_for_serial "$serial" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000000" || { echo quit; return 1; }
  wait_for_serial "$serial" "SYSCALL_CAPABILITY_DENIED app=/apps/hello.zex syscall=1 capability=console-write reason=missing-capability" || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }
  prompt_count="$(grep -c "$PROMPT" "$serial" || true)"
  send_command "run ZENOVAPP.ZEX"
  wait_for_serial "$serial" "ZGDB_REVOCATION_BLOCKED" || { echo quit; return 1; }
  wait_for_count "$serial" "$PROMPT" $((prompt_count + 1)) || { echo quit; return 1; }
  send_command "guard quarantine list"; wait_for_serial "$serial" "ZENOV_GUARD_QUARANTINE_LIST_OK entries=2" || { echo quit; return 1; }
  send_command "cat PERSIST.TXT"; wait_for_serial "$serial" "PERSISTENCE_0_1_1_OK" || { echo quit; return 1; }
  send_command "cat /data/apps/userio.txt"; wait_for_serial "$serial" "FILE_SYSCALL_PERSIST_OK" || { echo quit; return 1; }
  send_command "fsck"; wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  send_command "stat /data/apps/userio.txt"; wait_for_serial "$serial" "Checksum" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 3 || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_recovery() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZGDB_POLICY_VERSION_OK version=3" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOVFS_INTERRUPTED_WRITE_RECOVERED" || { echo quit; return 1; }
  send_command "cat /data/config/system.ini"
  wait_for_serial "$serial" "recovery=committed" || { echo quit; return 1; }
  send_command "fsck"
  wait_for_serial "$serial" "ZENOVFS_FSCK_OK" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 2 || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_audit_old_recovery() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOVFS_INTERRUPTED_WRITE_RECOVERED" || { echo quit; return 1; }
  grep -q 'ZENOV_GUARD_AUDIT_REPLAY_OK count=0 next=1' "$serial" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 2 || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_audit_new_recovery() {
  local serial="$1"
  wait_for_boot "$serial" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOVFS_INTERRUPTED_WRITE_RECOVERED" || { echo quit; return 1; }
  grep -q 'ZENOV_GUARD_AUDIT_REPLAY_OK count=1 next=2' "$serial" || { echo quit; return 1; }
  send_command "guard log verify"; wait_for_count "$serial" "ZENOV_GUARD_AUDIT_VERIFY_OK" 2 || { echo quit; return 1; }
  sleep 0.2; echo quit
}

controller_audit_corrupt() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "$STORAGE_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_SELFTEST_OK" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_INVALID" || { echo quit; return 1; }
  wait_for_serial "$serial" "Persistent ZenovGuard audit journal validation failed." || { echo quit; return 1; }
  if grep -q "$UI_MARKER" "$serial"; then echo "qemu-smoke: corrupt audit image reached UI" >&2; echo quit; return 1; fi
  echo quit
}

controller_zcap_corrupt() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "$STORAGE_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_READY" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZGDB_READY" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZCAP_INIT_FAILED reason=payload-digest" || { echo quit; return 1; }
  wait_for_serial "$serial" "Signed syscall capability policy validation failed." || { echo quit; return 1; }
  if grep -q "$UI_MARKER" "$serial"; then echo "qemu-smoke: corrupt ZCAP image reached UI" >&2; echo quit; return 1; fi
  echo quit
}

controller_zmid_corrupt() {
  local serial="$1"
  wait_for_serial "$serial" "$BOOT_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "$STORAGE_MARKER" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZENOV_GUARD_AUDIT_READY" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZGDB_READY" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZCAP_READY" || { echo quit; return 1; }
  wait_for_serial "$serial" "ZMID_INIT_FAILED reason=payload-digest" || { echo quit; return 1; }
  wait_for_serial "$serial" "Signed malware intelligence validation failed." || { echo quit; return 1; }
  if grep -q "$UI_MARKER" "$serial"; then echo "qemu-smoke: corrupt ZMID image reached UI" >&2; echo quit; return 1; fi
  echo quit
}

run_phase() {
  local controller="$1" serial="$2" monitor="$3" stderr="$4" data_image="$5"
  set +e
  "$controller" "$serial" | timeout 95s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$data_image,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    >"$monitor" 2>"$stderr"
  local status=$?; set -e
  if [[ $status -ne 0 ]]; then
    echo "qemu-smoke: phase failed with status $status" >&2; cat "$monitor" >&2 || true; cat "$stderr" >&2 || true; cat "$serial" >&2 || true; return 1
  fi
}

for required in "$RECOVERY_IMAGE" "$AUDIT_OLD_RECOVERY_IMAGE" "$AUDIT_NEW_RECOVERY_IMAGE" "$AUDIT_CORRUPT_IMAGE" "$ZCAP_CORRUPT_IMAGE" "$ZMID_CORRUPT_IMAGE"; do
  [[ -n "$required" && -f "$required" ]] || { echo "qemu-smoke: required recovery/fault image is missing: $required" >&2; exit 1; }
done
SERIAL1="$(cd "$OUT" && pwd)/serial-phase1.log"
SERIAL2="$(cd "$OUT" && pwd)/serial-phase2.log"
SERIAL3="$(cd "$OUT" && pwd)/serial-recovery.log"
SERIAL4="$(cd "$OUT" && pwd)/serial-audit-old-recovery.log"
SERIAL5="$(cd "$OUT" && pwd)/serial-audit-new-recovery.log"
SERIAL6="$(cd "$OUT" && pwd)/serial-audit-corrupt.log"
SERIAL7="$(cd "$OUT" && pwd)/serial-zcap-corrupt.log"
SERIAL8="$(cd "$OUT" && pwd)/serial-zmid-corrupt.log"
run_phase controller_first "$SERIAL1" "$OUT/monitor-phase1.log" "$OUT/qemu-phase1.stderr" "$DATA_IMAGE"
run_phase controller_second "$SERIAL2" "$OUT/monitor-phase2.log" "$OUT/qemu-phase2.stderr" "$DATA_IMAGE"
run_phase controller_recovery "$SERIAL3" "$OUT/monitor-recovery.log" "$OUT/qemu-recovery.stderr" "$RECOVERY_IMAGE"
run_phase controller_audit_old_recovery "$SERIAL4" "$OUT/monitor-audit-old-recovery.log" "$OUT/qemu-audit-old-recovery.stderr" "$AUDIT_OLD_RECOVERY_IMAGE"
run_phase controller_audit_new_recovery "$SERIAL5" "$OUT/monitor-audit-new-recovery.log" "$OUT/qemu-audit-new-recovery.stderr" "$AUDIT_NEW_RECOVERY_IMAGE"
run_phase controller_audit_corrupt "$SERIAL6" "$OUT/monitor-audit-corrupt.log" "$OUT/qemu-audit-corrupt.stderr" "$AUDIT_CORRUPT_IMAGE"
run_phase controller_zcap_corrupt "$SERIAL7" "$OUT/monitor-zcap-corrupt.log" "$OUT/qemu-zcap-corrupt.stderr" "$ZCAP_CORRUPT_IMAGE"
run_phase controller_zmid_corrupt "$SERIAL8" "$OUT/monitor-zmid-corrupt.log" "$OUT/qemu-zmid-corrupt.stderr" "$ZMID_CORRUPT_IMAGE"
cat "$SERIAL1" "$SERIAL2" "$SERIAL3" "$SERIAL4" "$SERIAL5" "$SERIAL6" "$SERIAL7" "$SERIAL8" > "$OUT/serial.log"

for marker in \
  "$BOOT_MARKER" "$PMM_MARKER" "PMM_STRESS_OK" "$PAGING_MARKER" "HEAP_REUSE_OK" "HEAP_COALESCE_OK" "HEAP_INVALID_FREE_BLOCKED" "HEAP_STRESS_OK" \
  "$STORAGE_MARKER" "$PROCESS_MARKER" "ZENOV_GUARD_AUDIT_SELFTEST_OK" "ZENOV_GUARD_AUDIT_REPLAY_OK" "ZENOV_GUARD_AUDIT_READY" "ZENOV_GUARD_AUDIT_VERIFY_OK" \
  "ZENOV_GUARD_SELFTEST_OK" "ZENOV_GUARD_TRUST_BASELINE_OK" "ZENOV_GUARD_READY" "ZENOV_GUARD_STATUS_OK" \
  "ZGDB_ROOT_KEY_OK id=6f788074c018f5aa" "ZGDB_PSS_SIGNATURE_OK" "ZGDB_POLICY_VERSION_OK version=3" "ZGDB_POLICY_VERSION_OK version=4" "ZGDB_READY" \
  "ZGDB_KEY_REJECTED reason=unknown-key" "ZGDB_TAMPER_REJECTED" "ZGDB_ATOMIC_UPDATE_OK version=4" "ZGDB_ROLLBACK_REJECTED" "ZGDB_REVOCATION_BLOCKED" \
  "ZCAP_ROOT_KEY_OK id=9202c73fad96ad66" "ZCAP_PSS_SIGNATURE_OK" "ZCAP_POLICY_VERSION_OK version=1" "ZCAP_POLICY_VERSION_OK version=2" "ZCAP_READY" \
  "ZCAP_KEY_REJECTED reason=unknown-key" "ZCAP_TAMPER_REJECTED" "ZCAP_ATOMIC_UPDATE_OK version=2" "ZCAP_ROLLBACK_REJECTED" \
  "ZMID_ROOT_KEY_OK id=6ca6a5275544c533" "ZMID_PSS_SIGNATURE_OK" "ZMID_DATABASE_VERSION_OK version=1" "ZMID_DATABASE_VERSION_OK version=2" "ZMID_READY" \
  "ZMID_KEY_REJECTED reason=unknown-key" "ZMID_TAMPER_REJECTED reason=payload-digest" "ZMID_ATOMIC_UPDATE_OK version=2" "ZMID_ROLLBACK_REJECTED reason=rollback" \
  "ZENOV_GUARD_WRITE_BLOCKED" "ZENOV_GUARD_WRITE_AUDIT" "ZENOV_GUARD_QUARANTINE_LIST_OK" "ZENOV_GUARD_DETECTED" "ZENOV_GUARD_QUARANTINE_OK" "ZENOV_GUARD_UNTRUSTED_BLOCKED" "ZENOV_GUARD_FULL_SCAN_OK" "ZENOV_GUARD_EXEC_ALLOWED" \
  "GRAPHICS_PCI_OK" "FRAMEBUFFER_MAPPED_OK" "GRAPHICS_MODE_OK" "BACKBUFFER_PRESENT_OK" \
  "CLIPPING_OK" "ALPHA_BLEND_OK" "FONT_RENDER_OK" "DESKTOP_SCENE_OK" "GRAPHICAL_DESKTOP_READY" "PS2_MOUSE_OK" "PS2_MOUSE_IRQ_ROUTE_OK" \
  "MOUSE_PACKET_OK" "WINDOW_DRAG_OK" "PS2_MOUSE_DECODER_OK" "$UI_MARKER" "$LONG_INPUT_MARKER" "WRITE_OK" "HELLO_ZEX_0_1_1_OK" \
  "FILEIO_ELF_OK" "FILE_SYSCALL_PERSIST_OK" "PROCESS_ARGV_OK" "SYSCALL_ERRORS_OK" "SYSCALL_POINTER_GUARD_OK" "CONSOLE_READ_SYSCALL_OK" \
  "PAGE_PROTECTION_OK" "USER_WRITE_TO_TEXT_BLOCKED" "USER_KERNEL_ACCESS_BLOCKED" "PAGE_FAULT_DIAGNOSTICS_OK" "USER_FAULT_RETURNED_TO_SHELL" \
  "ZENOV_SOURCE_APP_RING3_OK" "ZENOV_COMPILER_ABI_MATCH_OK" "ZENOVFS_INTERRUPTED_WRITE_RECOVERED" "recovery=committed" "ZENOVFS_FSCK_OK" \
  "ZENOV_GUARD_AUDIT_INVALID" "Persistent ZenovGuard audit journal validation failed." "ZCAP_INIT_FAILED reason=payload-digest" "Signed syscall capability policy validation failed." \
  "ZMID_INIT_FAILED reason=payload-digest" "Signed malware intelligence validation failed."; do
  grep -q "$marker" "$OUT/serial.log" || { echo "qemu-smoke: missing marker: $marker" >&2; exit 1; }
done
[[ "$(grep -c 'ZENOV_GUARD_AUDIT_VERIFY_OK' "$OUT/serial.log")" -ge 12 ]] || { echo "qemu-smoke: persistent audit verification count is too low" >&2; exit 1; }
grep -Eq 'ZENOV_GUARD_AUDIT_REPLAY_OK count=[1-9][0-9]*' "$SERIAL2" || { echo "qemu-smoke: persistent audit did not replay non-empty state" >&2; exit 1; }
grep -q 'ZENOV_GUARD_AUDIT_REPLAY_OK count=0 next=1' "$SERIAL4" || { echo "qemu-smoke: old audit transaction did not recover old journal" >&2; exit 1; }
grep -q 'ZENOV_GUARD_AUDIT_REPLAY_OK count=1 next=2' "$SERIAL5" || { echo "qemu-smoke: committed audit transaction did not recover new journal" >&2; exit 1; }
! grep -q "$UI_MARKER" "$SERIAL6" || { echo "qemu-smoke: invalid audit journal reached UI" >&2; exit 1; }
! grep -q "$UI_MARKER" "$SERIAL7" || { echo "qemu-smoke: invalid ZCAP policy reached UI" >&2; exit 1; }
! grep -q "$UI_MARKER" "$SERIAL8" || { echo "qemu-smoke: invalid ZMID database reached UI" >&2; exit 1; }
[[ "$(grep -c 'ZENOVFS_INTERRUPTED_WRITE_RECOVERED' "$OUT/serial.log")" -ge 3 ]] || { echo "qemu-smoke: expected recovery phases were not observed" >&2; exit 1; }
[[ "$(grep -c 'ZENOV_GUARD_EXEC_ALLOWED' "$OUT/serial.log")" -ge 7 ]] || { echo "qemu-smoke: trusted application appraisal count is too low" >&2; exit 1; }
[[ "$(grep -c 'ZGDB_REVOCATION_BLOCKED' "$OUT/serial.log")" -ge 2 ]] || { echo "qemu-smoke: revocation did not persist across reboot" >&2; exit 1; }
[[ "$(grep -c 'ZGDB_PSS_SIGNATURE_OK' "$OUT/serial.log")" -ge 6 ]] || { echo "qemu-smoke: ZGDB PSS verification missing from a required phase" >&2; exit 1; }
[[ "$(grep -c 'ZCAP_PSS_SIGNATURE_OK' "$OUT/serial.log")" -ge 6 ]] || { echo "qemu-smoke: ZCAP PSS verification missing from a successful boot phase" >&2; exit 1; }
[[ "$(grep -c 'ZMID_PSS_SIGNATURE_OK' "$OUT/serial.log")" -ge 5 ]] || { echo "qemu-smoke: ZMID PSS verification missing from a successful boot phase" >&2; exit 1; }
[[ "$(grep -c 'SYSCALL_CAPABILITY_DENIED app=/apps/hello.zex' "$OUT/serial.log")" -ge 2 ]] || { echo "qemu-smoke: signed capability revocation did not persist" >&2; exit 1; }
[[ "$(grep -c 'APP_EXIT code=0' "$OUT/serial.log")" -ge 5 ]] || { echo "qemu-smoke: successful applications did not all exit cleanly" >&2; exit 1; }
[[ "$(grep -c 'Application could not be loaded' "$OUT/serial.log")" -eq 3 ]] || { echo "qemu-smoke: unexpected application load failure count" >&2; exit 1; }
[[ "$(grep -c 'PERSISTENCE_0_1_1_OK' "$OUT/serial.log")" -ge 2 ]] || { echo "qemu-smoke: shell persistence marker missing across reboot" >&2; exit 1; }
[[ "$(grep -c 'FILE_SYSCALL_PERSIST_OK' "$OUT/serial.log")" -ge 2 ]] || { echo "qemu-smoke: userspace file payload missing across reboot" >&2; exit 1; }
[[ -s "$SCREENSHOT" ]] || { echo "qemu-smoke: graphical framebuffer screenshot missing" >&2; exit 1; }
printf 'qemu-smoke: OK 0.1.1 persistent-audit crash-prefix torn-sector garbage dropped-write reorder old-new recovery fail-closed ZGDB2+ZCAP1+ZMID1 RSA-PSS on-write-prevention graphical-desktop serial=%s screenshot=%s\n' "$OUT/serial.log" "$SCREENSHOT"
