#!/usr/bin/env bash
set -euo pipefail

SERIAL="${1:-build/qemu/serial.log}"
RUNTIME_IMAGE="${2:-build/qemu/zenov-data-runtime.img}"
VERIFIER="${3:-build/zenovfs-audit-verify}"
EVIDENCE="${4:-build/qemu/syscall-capability-evidence.txt}"
PHASE1="$(dirname "$SERIAL")/serial-phase1.log"
PHASE2="$(dirname "$SERIAL")/serial-phase2.log"
ZCAP_CORRUPT="$(dirname "$SERIAL")/serial-zcap-corrupt.log"

for required in "$SERIAL" "$PHASE1" "$PHASE2" "$ZCAP_CORRUPT" "$RUNTIME_IMAGE" "$VERIFIER"; do
  [[ -e "$required" ]] || { echo "syscall-capability-check: missing $required" >&2; exit 1; }
done
[[ -x "$VERIFIER" ]] || { echo "syscall-capability-check: verifier is not executable" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SERIAL_TEXT="$TMP/serial.log"
PHASE1_TEXT="$TMP/serial-phase1.log"
PHASE2_TEXT="$TMP/serial-phase2.log"
ZCAP_CORRUPT_TEXT="$TMP/serial-zcap-corrupt.log"
tr -d '\r' <"$SERIAL" >"$SERIAL_TEXT"
tr -d '\r' <"$PHASE1" >"$PHASE1_TEXT"
tr -d '\r' <"$PHASE2" >"$PHASE2_TEXT"
tr -d '\r' <"$ZCAP_CORRUPT" >"$ZCAP_CORRUPT_TEXT"

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

[[ "$(grep -Fc 'ZCAP_ROOT_KEY_OK id=9202c73fad96ad66' "$SERIAL_TEXT")" -ge 5 ]] || { echo "syscall-capability-check: ZCAP root missing from successful boots" >&2; exit 1; }
[[ "$(grep -Fc 'ZCAP_PSS_SIGNATURE_OK' "$SERIAL_TEXT")" -ge 5 ]] || { echo "syscall-capability-check: ZCAP signature verification missing" >&2; exit 1; }
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_POLICY_OK' "$SERIAL_TEXT")" -ge 5 ]] || { echo "syscall-capability-check: policy missing from successful boots" >&2; exit 1; }
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILES_OK count=7' "$SERIAL_TEXT")" -ge 5 ]] || { echo "syscall-capability-check: seven-profile bijection not confirmed" >&2; exit 1; }

require_line "$PHASE1_TEXT" "ZCAP_POLICY_VERSION_OK version=1"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000001"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/fileio.elf mask=0x0000007D"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/args.elf mask=0x00000005"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/console.elf mask=0x00000081"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/protect.elf mask=0x00000001"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/kaccess.elf mask=0x00000000"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/zenovapp.zex mask=0x00000001"
require_line "$PHASE1_TEXT" "ZCAP_KEY_REJECTED reason=unknown-key"
require_line "$PHASE1_TEXT" "ZCAP_TAMPER_REJECTED reason=payload-digest"
require_line "$PHASE1_TEXT" "ZCAP_ATOMIC_UPDATE_OK version=2"
require_line "$PHASE1_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000000"
require_line "$PHASE1_TEXT" "ZCAP_ROLLBACK_REJECTED reason=rollback"
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=' "$PHASE1_TEXT")" -eq 11 ]] || { echo "syscall-capability-check: primary phase must contain seven baseline, three ransomware-regression FILEIO and one post-update activation" >&2; exit 1; }
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/fileio.elf mask=0x0000007D' "$PHASE1_TEXT")" -eq 4 ]] || { echo "syscall-capability-check: primary phase must contain exactly four FILEIO activations" >&2; exit 1; }

KACCESS_DENIAL="SYSCALL_CAPABILITY_DENIED app=/apps/kaccess.elf syscall=1 capability=console-write reason=missing-capability"
HELLO_DENIAL="SYSCALL_CAPABILITY_DENIED app=/apps/hello.zex syscall=1 capability=console-write reason=missing-capability"
require_line "$PHASE1_TEXT" "$KACCESS_DENIAL"
require_line "$PHASE1_TEXT" "$HELLO_DENIAL"
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_DENIED app=' "$PHASE1_TEXT")" -eq 2 ]] || { echo "syscall-capability-check: primary phase must contain exactly the KACCESS and post-update HELLO denials" >&2; exit 1; }

kaccess_start="$(line_number "$PHASE1_TEXT" 'APP_START_ELF /data/apps/kaccess.elf')"
kaccess_denial="$(line_number "$PHASE1_TEXT" "$KACCESS_DENIAL")"
fault_line="$(line_number "$PHASE1_TEXT" 'USER_KERNEL_ACCESS_BLOCKED')"
zenov_profile="$(line_number "$PHASE1_TEXT" 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/zenovapp.zex mask=0x00000001')"
zenov_success="$(line_number "$PHASE1_TEXT" 'ZENOV_SOURCE_APP_RING3_OK')"
update_line="$(line_number "$PHASE1_TEXT" 'ZCAP_ATOMIC_UPDATE_OK version=2')"
hello_v2_profile="$(line_number "$PHASE1_TEXT" 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000000')"
hello_denial="$(line_number "$PHASE1_TEXT" "$HELLO_DENIAL")"
[[ -n "$kaccess_start" && -n "$kaccess_denial" && -n "$fault_line" && -n "$zenov_profile" && -n "$zenov_success" && -n "$update_line" && -n "$hello_v2_profile" && -n "$hello_denial" ]] || { echo "syscall-capability-check: ordering evidence incomplete" >&2; exit 1; }
(( kaccess_start < kaccess_denial && kaccess_denial < fault_line && fault_line < zenov_profile && zenov_profile < zenov_success )) || {
  echo "syscall-capability-check: denial/clear/reactivation order is invalid" >&2; exit 1;
}
(( zenov_success < update_line && update_line < hello_v2_profile && hello_v2_profile < hello_denial )) || {
  echo "syscall-capability-check: signed policy activation order is invalid" >&2; exit 1;
}

require_line "$PHASE2_TEXT" "ZCAP_POLICY_VERSION_OK version=2"
require_line "$PHASE2_TEXT" "SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/hello.zex mask=0x00000000"
require_line "$PHASE2_TEXT" "$HELLO_DENIAL"
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=' "$PHASE2_TEXT")" -eq 3 ]] || { echo "syscall-capability-check: reboot phase must contain HELLO plus two ransomware-regression FILEIO activations" >&2; exit 1; }
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_PROFILE_ACTIVE app=/apps/fileio.elf mask=0x0000007D' "$PHASE2_TEXT")" -eq 2 ]] || { echo "syscall-capability-check: reboot phase must contain exactly two FILEIO activations" >&2; exit 1; }
[[ "$(grep -Fc 'SYSCALL_CAPABILITY_DENIED app=' "$PHASE2_TEXT")" -eq 1 ]] || { echo "syscall-capability-check: reboot phase has unexpected denials" >&2; exit 1; }

require_line "$ZCAP_CORRUPT_TEXT" "ZCAP_INIT_FAILED reason=payload-digest"
require_line "$ZCAP_CORRUPT_TEXT" "ZENOVOS KERNEL PANIC"
require_line "$ZCAP_CORRUPT_TEXT" "Signed syscall capability policy validation failed."
! grep -Fq 'ZENOVOS_UI_READY' "$ZCAP_CORRUPT_TEXT" || { echo "syscall-capability-check: corrupted signed policy reached UI" >&2; exit 1; }

forbid_marker "SYSCALL_CAPABILITY_POLICY_FAILED"
forbid_marker "SYSCALL_CAPABILITY_POLICY_NOT_READY"
forbid_marker "SYSCALL_CAPABILITY_PROFILE_MISSING"
forbid_marker "SYSCALL_CAPABILITY_ACTIVATION_FAILED"
forbid_marker "SYSCALL_CAPABILITY_AUDIT_FAILED"
forbid_marker "ZCAP_SEQUENCE_REJECTED"

"$VERIFIER" "$RUNTIME_IMAGE" --require-nonempty --require-exec-untrusted /apps/kaccess.elf
"$VERIFIER" "$RUNTIME_IMAGE" --require-nonempty --require-exec-untrusted /apps/hello.zex

cat >"$EVIDENCE" <<EOF2
ZENOV_SYSCALL_CAPABILITY_EVIDENCE_V2
version=0.1.1
format=ZCAP1
schema=1
root_key_id=9202c73fad96ad66
signature=RSA-2048-PSS-SHA256-MGF1-SHA256-salt32
policy_transition=1->2
profiles=7
v1_hello_mask=0x00000001
v2_hello_mask=0x00000000
denied_error=0xfffffff9
file_scope=exact-normalized-path
ring3_denial=/apps/kaccess.elf:syscall-1:console-write
signed_revocation=/apps/hello.zex:syscall-1:console-write
persistent_records=EXEC:UNTRUSTED:/apps/kaccess.elf,EXEC:UNTRUSTED:/apps/hello.zex
rollback=blocked
corrupt_policy_boot=fail-closed-before-ui
authority_reactivation=/apps/zenovapp.zex:0x00000001
ransomware_regression_profiles=phase1-fileio-4,phase2-fileio-2
status=PASS
EOF2

grep -Fqx 'status=PASS' "$EVIDENCE"
echo "ZENOV_SYSCALL_CAPABILITY_GATE_OK profiles=7 signed_update=yes persistent_denials=2 authority_leakage=no"
