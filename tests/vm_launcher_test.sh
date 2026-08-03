#!/usr/bin/env bash
set -euo pipefail

HELPER_SOURCE="${1:-packaging/prepare-vm.sh}"
OUT_INPUT="${2:-build/vm-launcher-test}"

fail() {
  printf 'vm-launcher-test: %s\n' "$*" >&2
  exit 1
}

[[ -f "$HELPER_SOURCE" ]] || fail "missing helper: $HELPER_SOURCE"
OUT_PARENT="$(dirname "$OUT_INPUT")"
OUT_NAME="$(basename "$OUT_INPUT")"
mkdir -p -- "$OUT_PARENT"
OUT="$(cd "$OUT_PARENT" && pwd -P)/$OUT_NAME"
rm -rf -- "$OUT"
mkdir -p -- "$OUT/bin" "$OUT/dist" "$OUT/state"
cp "$HELPER_SOURCE" "$OUT/dist/prepare-vm.sh"
chmod +x "$OUT/dist/prepare-vm.sh"
printf 'iso-fixture' > "$OUT/dist/ZenovOS-0.1.1-x86.iso"
printf 'raw-fixture' > "$OUT/dist/ZenovOS-0.1.1-data.img"
printf 'vdi-fixture' > "$OUT/dist/ZenovOS-0.1.1-data.vdi"
(
  cd "$OUT/dist"
  sha256sum ZenovOS-0.1.1-x86.iso ZenovOS-0.1.1-data.img > SHA256SUMS.txt
)

cat > "$OUT/bin/VBoxManage" <<'VBOX'
#!/usr/bin/env bash
set -euo pipefail
printf 'VBoxManage' >> "${TEST_LOG:?}"
printf ' %q' "$@" >> "$TEST_LOG"
printf '\n' >> "$TEST_LOG"

exists_file="${TEST_STATE_DIR:?}/exists"
state_file="$TEST_STATE_DIR/state"
state="poweroff"
[[ -f "$state_file" ]] && state="$(cat "$state_file")"
case "${1:-}" in
  showvminfo)
    [[ -f "$exists_file" ]] || exit 1
    if [[ "${3:-}" == "--machinereadable" ]]; then
      printf 'VMState="%s"\n' "$state"
      printf 'storagecontrollername0="IDE Controller"\n'
      printf 'storagecontrollertype0="PIIX4"\n'
      if [[ "${TEST_ATTACHMENTS:-present}" == present ]]; then
        printf 'IDE Controller-0-0="%s/ZenovOS-0.1.1-data.vdi"\n' "${TEST_DIST:?}"
        printf 'IDE Controller-1-0="%s/ZenovOS-0.1.1-x86.iso"\n' "$TEST_DIST"
      fi
    fi
    ;;
  createvm)
    : > "$exists_file"
    ;;
  discardstate)
    printf 'poweroff' > "$state_file"
    ;;
  startvm)
    case "${VBOX_START_MODE:-success}" in
      success)
        printf 'VM started\n'
        ;;
      svm-disabled)
        printf 'AMD-V is disabled in the BIOS (or by the host OS) (VERR_SVM_DISABLED).\n' >&2
        exit 1
        ;;
      svm-in-use)
        printf 'AMD-V is being used by another hypervisor (VERR_SVM_IN_USE).\n' >&2
        exit 1
        ;;
      generic)
        printf 'Generic VirtualBox failure.\n' >&2
        exit 1
        ;;
      *) exit 2 ;;
    esac
    ;;
  *) ;;
esac
VBOX

cat > "$OUT/bin/qemu-system-i386" <<'QEMU'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == -accel && "${2:-}" == help ]]; then
  printf 'tcg\nkvm\n'
  exit 0
fi
printf 'QEMU' >> "${TEST_LOG:?}"
printf ' %q' "$@" >> "$TEST_LOG"
printf '\n' >> "$TEST_LOG"
QEMU

cat > "$OUT/bin/qemu-img" <<'QEMUIMG'
#!/usr/bin/env bash
set -euo pipefail
printf 'qemu-img' >> "${TEST_LOG:?}"
printf ' %q' "$@" >> "$TEST_LOG"
printf '\n' >> "$TEST_LOG"
if [[ "${1:-}" == convert ]]; then
  cp "${@: -2:1}" "${@: -1}"
fi
QEMUIMG
chmod +x "$OUT/bin/VBoxManage" "$OUT/bin/qemu-system-i386" "$OUT/bin/qemu-img"

run_helper() {
  PATH="$OUT/bin:$PATH" \
  TEST_LOG="$OUT/log" \
  TEST_STATE_DIR="$OUT/state" \
  TEST_DIST="$OUT/dist" \
  ZENOV_VM_NAME=gergre \
  ZENOV_QEMU_ACCEL=tcg \
  "$OUT/dist/prepare-vm.sh" "$@"
}

reset_fixture() {
  : > "$OUT/log"
  rm -f -- "$OUT/state/exists" "$OUT/state/state"
}

reset_fixture
: > "$OUT/state/exists"
VBOX_START_MODE=svm-disabled run_helper virtualbox > "$OUT/fallback.out" 2> "$OUT/fallback.err"
grep -Fq 'Repairing existing VirtualBox VM: gergre' "$OUT/fallback.out"
grep -Fq VERR_SVM_DISABLED "$OUT/fallback.err"
grep -Fq 'Starting ZenovOS with QEMU software-compatible path' "$OUT/fallback.err"
grep -Fq 'QEMU -accel tcg\,thread=multi' "$OUT/log"
grep -Fq 'file='"$OUT"'/dist/ZenovOS-0.1.1-data.vdi\,format=vdi' "$OUT/log"
grep -Fq 'VBoxManage modifyvm gergre' "$OUT/log"
grep -Fq -- '--cpus 1' "$OUT/log"
grep -Fq -- '--firmware bios' "$OUT/log"

reset_fixture
: > "$OUT/state/exists"
VBOX_START_MODE=success run_helper virtualbox > "$OUT/vbox-success.out" 2> "$OUT/vbox-success.err"
grep -Fq 'VM started' "$OUT/vbox-success.out"
! grep -Fq 'QEMU ' "$OUT/log"

reset_fixture
ZENOV_VM_START=0 run_helper virtualbox > "$OUT/create.out" 2> "$OUT/create.err"
grep -Fq 'VirtualBox appliance prepared: gergre' "$OUT/create.out"
grep -Fq 'VBoxManage createvm --name gergre' "$OUT/log"
! grep -Fq 'VBoxManage startvm' "$OUT/log"

reset_fixture
: > "$OUT/state/exists"
if ZENOV_VM_SOFTWARE_FALLBACK=0 VBOX_START_MODE=svm-disabled \
    run_helper virtualbox > "$OUT/no-fallback.out" 2> "$OUT/no-fallback.err"; then
  fail 'hardware-virtualization failure unexpectedly succeeded with fallback disabled'
fi
grep -Fq VERR_SVM_DISABLED "$OUT/no-fallback.err"
! grep -Fq 'QEMU ' "$OUT/log"

reset_fixture
: > "$OUT/state/exists"
printf 'running' > "$OUT/state/state"
if ZENOV_VM_START=0 run_helper virtualbox > "$OUT/running.out" 2> "$OUT/running.err"; then
  fail 'repair unexpectedly modified a running VM'
fi
grep -Fq "must be fully powered off" "$OUT/running.err"
! grep -Fq 'VBoxManage modifyvm' "$OUT/log"

reset_fixture
run_helper qemu > "$OUT/qemu.out" 2> "$OUT/qemu.err"
grep -Fq 'QEMU -accel tcg\,thread=multi' "$OUT/log"
grep -Fq 'format=qcow2' "$OUT/log"

printf 'VM_LAUNCHER_OK existing=repair vbox=start fallback=tcg errors=fail-closed qemu=tcg vm=gergre\n'
