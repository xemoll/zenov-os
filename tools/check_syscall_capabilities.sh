#!/usr/bin/env bash
set -euo pipefail

SERIAL="${1:-build/qemu/serial.log}"
RUNTIME_IMAGE="${2:-build/qemu/zenov-data-runtime.img}"
VERIFIER="${3:-build/zenovfs-audit-verify}"
EVIDENCE="${4:-build/qemu/syscall-capability-evidence.txt}"
PHASE1="$(dirname "$SERIAL")/serial-phase1.log"
PHASE2="$(dirname "$SERIAL")/serial-phase2.log"

for required in "$SERIAL" "$PHASE1" "$PHASE2" "$RUNTIME_IMAGE" "$VERIFIER"; do
  [[ -e "$required" ]] || { echo "syscall-capability-check: missing $required" >&2; exit 1; }
done
[[ -x "$VERIFIER" ]] || { echo "syscall-capability-check: verifier is not executable" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SERIAL_TEXT="$TMP/serial.log"
PHASE1_TEXT="$TMP/serial-phase1.log"
PHASE2_TEXT="$TMP/serial-phase2.log"
tr -d '\r' <"$SERIAL" >"$SERIAL_TEXT"
tr -d '\r' <"$PHASE1" >"$PHASE1_TEXT"
tr -d '\r' <"$PHASE2" >"$PHASE2_TEXT"

require_line() {
  local file="$1" text="$2"
  grep -Fqx "$text" "$file" || { echo "syscall-capability-check: missing exact line: $text" >&2; exit 1; }
}
forbid_marker() {
  local marker="$1"
  ! grep -Fq "$marker" "$SERIAL_TEXT" || { echo "syscall-capability-check: forbidden marker: $marker" >&2; exit 1; }
}
line_number() {
  local file="$1" text="$2"
  grep -Fn "$text" "$file" | head -n1 | cut -d: -f1
}

[[ "$(grep -Fc 'SYSCALL_CAPABILITY_POLICY_OK' "$SERIAL_TEXT")" -ge 5 ]] || { echo "syscall-capability-check: policy missing from successful boots" >&2; exit 1; }
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILES_OK count=7' "$SERIAL_TEXT")" -ge 5 ]] || { echo "syscall-capability-check: seven-profile bijection not confirmed" >&2; exit 1; }

require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000001"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/fileio.elf mask=0x0000007D"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/args.elf mask=0x00000005"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/console.elf mask=0x00000081"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/protect.elf mask=0x00000001"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/kaccess.elf mask=0x00000000"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/zenovapp.zex mask=0x00000001"
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=' "$PHASE1_TEXT")" -eq 7 ]] || { echo "syscall-capability-check: unexpected profile activation count" >&2; exit 1; }

DENIAL="SYSCALL_CAPABILITY_DENIED app=/apps/kaccess.elf syscall=1 capability=console-write reason=missing-capability"
require_line "$PHASE1_TEXT" "$DENIAL"
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_DENIED app=' "$PHASE1_TEXT")" -eq 1 ]] || { echo "syscall-capability-check: unexpected denial count" >&2; exit 1; }

kaccess_start="$(line_number "$PHASE1_TEXT" 'APP_START_ELF /data/apps/kaccess.elf')"
denial_line="$(line_number "$PHASE1_TEXT" "$DENIAL")"
fault_line="$(line_number "$PHASE1_TEXT" 'USER_KERNEL_ACCESS_BLOCKED')"
zenov_profile="$(line_number "$PHASE1_TEXT" 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/zenovapp.zex mask=0x00000001')"
zenov_success="$(line_number "$PHASE1_TEXT" 'ZENOV_SOURCE_APP_RING3_OK')"
[[ -n "$kaccess_start" && -n "$denial_line" && -n "$fault_line" && -n "$zenov_profile" && -n "$zenov_success" ]] || { echo "syscall-capability-check: ordering evidence incomplete" >&2; exit 1; }
(( kaccess_start < denial_line && denial_line < fault_line && fault_line < zenov_profile && zenov_profile < zenov_success )) || {
  echo "syscall-capability-check: denial/clear/reactivation order is invalid" >&2; exit 1;
}

forbid_marker "SYSCALL_CAPABILITY_POLICY_FAILED"
forbid_marker "SYSCALL_CAPABILITY_POLICY_NOT_READY"
forbid_marker "SYSCALL_CAPABILITY_PROFILE_MISSING"
forbid_marker "SYSCALL_CAPABILITY_ACTIVATION_FAILED"
forbid_marker "SYSCALL_CAPABILITY_AUDIT_FAILED"

"$VERIFIER" "$RUNTIME_IMAGE" --require-nonempty --require-exec-untrusted /apps/kaccess.elf

cat >"$EVIDENCE" <<EOF
ZENOV_SYSCALL_CAPABILITY_EVIDENCE_V1
version=0.1.1
profiles=7
denied_error=0xfffffff9
file_scope=exact-normalized-path
ring3_denial=/apps/kaccess.elf:syscall-1:console-write
persistent_record=EXEC:UNTRUSTED:/apps/kaccess.elf
authority_reactivation=/apps/zenovapp.zex:0x00000001
status=PASS
EOF

grep -Fqx 'status=PASS' "$EVIDENCE"
echo "ZENOV_SYSCALL_CAPABILITY_GATE_OK profiles=7 persistent_denial=yes authority_leakage=no"
