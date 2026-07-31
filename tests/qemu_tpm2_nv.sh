#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
SWTPM="${SWTPM:-swtpm}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/tpm2-nv}"
PROMPT='zenov> '

mkdir -p "$OUT"
rm -rf "$OUT/tpm-state"
rm -f "$OUT"/*.log "$OUT"/*.stderr "$OUT"/*.img "$OUT"/*.sock "$OUT"/*.pid
mkdir -p "$OUT/tpm-state"

wait_for_serial() {
  local file="$1" text="$2" timeout_tenths="${3:-1200}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-tpm2-nv: missing serial marker: $text" >&2
  return 1
}

send_text() {
  local text="$1" char lower i
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="$(printf '%s' "$char" | tr 'A-Z' 'a-z')"; echo "sendkey shift-$lower 10" ;;
      ' ') echo "sendkey spc 10" ;;
      '-') echo "sendkey minus 10" ;;
      *) echo "qemu-tpm2-nv: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.012
  done
}
send_command() { send_text "$1"; echo "sendkey ret 10"; }

start_swtpm() {
  local phase="$1" socket="$OUT/swtpm.sock" log="$OUT/swtpm-$phase.log"
  rm -f "$socket"
  "$SWTPM" socket --tpm2 \
    --tpmstate "dir=$OUT/tpm-state" \
    --ctrl "type=unixio,path=$socket" \
    --log "file=$log,level=20" \
    >"$OUT/swtpm-$phase.stdout" 2>"$OUT/swtpm-$phase.stderr" &
  SWTPM_PID=$!
  printf '%s\n' "$SWTPM_PID" > "$OUT/swtpm.pid"
  for _ in $(seq 1 200); do
    [[ -S "$socket" ]] && return 0
    kill -0 "$SWTPM_PID" 2>/dev/null || {
      cat "$OUT/swtpm-$phase.stderr" >&2 || true
      return 1
    }
    sleep 0.05
  done
  echo 'qemu-tpm2-nv: swtpm control socket did not appear' >&2
  return 1
}

stop_swtpm() {
  if [[ -n "${SWTPM_PID:-}" ]]; then
    kill "$SWTPM_PID" 2>/dev/null || true
    wait "$SWTPM_PID" 2>/dev/null || true
    SWTPM_PID=''
  fi
  rm -f "$OUT/swtpm.sock"
}
trap stop_swtpm EXIT

controller_provision() {
  local serial="$1"
  wait_for_serial "$serial" 'TPM2_TIS_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'TPM2_NV_COUNTER_UNPROVISIONED index=0x015a4f53' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  send_command 'tpm status'
  wait_for_serial "$serial" 'TPM2_STATUS transport=ready counter=unprovisioned' || { echo quit; return 1; }
  send_command 'tpm provision'
  wait_for_serial "$serial" 'TPM2_NV_COUNTER_PROVISIONED generation=1 index=0x015a4f53' || { echo quit; return 1; }
  send_command 'tpm increment'
  wait_for_serial "$serial" 'TPM2_NV_COUNTER_INCREMENTED generation=2' || { echo quit; return 1; }
  send_command 'tpm selftest'
  wait_for_serial "$serial" 'TPM2_SELFTEST_OK generation=2' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

controller_reboot() {
  local serial="$1"
  wait_for_serial "$serial" 'TPM2_TIS_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'TPM2_NV_COUNTER_READY generation=2 index=0x015a4f53' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  send_command 'tpm status'
  wait_for_serial "$serial" 'TPM2_STATUS transport=ready counter=2' || { echo quit; return 1; }
  send_command 'tpm increment'
  wait_for_serial "$serial" 'TPM2_NV_COUNTER_INCREMENTED generation=3' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

controller_persistent() {
  local serial="$1"
  wait_for_serial "$serial" 'TPM2_NV_COUNTER_READY generation=3 index=0x015a4f53' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  send_command 'tpm selftest'
  wait_for_serial "$serial" 'TPM2_SELFTEST_OK generation=3' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

controller_absent() {
  local serial="$1"
  wait_for_serial "$serial" 'TPM2_TIS_UNAVAILABLE' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

run_phase() {
  local phase="$1" controller="$2" with_tpm="$3"
  local serial="$OUT/serial-$phase.log" monitor="$OUT/monitor-$phase.log" stderr="$OUT/qemu-$phase.stderr"
  local data="$OUT/data-$phase.img" status
  cp "$DATA_IMAGE" "$data"
  cmp "$DATA_IMAGE" "$data"
  local -a tpm_args=()
  if [[ "$with_tpm" == yes ]]; then
    start_swtpm "$phase"
    tpm_args=(
      -chardev "socket,id=chrtpm,path=$OUT/swtpm.sock"
      -tpmdev emulator,id=tpm0,chardev=chrtpm
      -device tpm-tis,tpmdev=tpm0
    )
  fi
  set +e
  "$controller" "$serial" | timeout 180s "$QEMU" \
    -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
    -drive "file=$data,format=raw,if=ide,index=0,media=disk" \
    -boot a -m 32M -machine pc,vmport=off -vga std -display none \
    -serial "file:$serial" -monitor stdio -no-reboot -no-shutdown \
    "${tpm_args[@]}" >"$monitor" 2>"$stderr"
  status=$?
  set -e
  [[ "$with_tpm" == yes ]] && stop_swtpm
  if [[ $status -ne 0 ]]; then
    echo "qemu-tpm2-nv: phase $phase failed with status $status" >&2
    cat "$monitor" >&2 || true
    cat "$stderr" >&2 || true
    cat "$serial" >&2 || true
    exit 1
  fi
  if [[ -s "$stderr" ]]; then
    echo "qemu-tpm2-nv: phase $phase produced QEMU stderr" >&2
    cat "$stderr" >&2
    exit 1
  fi
}

command -v "$QEMU" >/dev/null
command -v "$SWTPM" >/dev/null
[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" ]] || {
  echo 'qemu-tpm2-nv: boot and data images are required' >&2
  exit 2
}

run_phase provision controller_provision yes
run_phase reboot controller_reboot yes
run_phase persistent controller_persistent yes
run_phase absent controller_absent no

grep -Fq 'TPM2_NV_COUNTER_PROVISIONED generation=1' "$OUT/serial-provision.log"
grep -Fq 'TPM2_NV_COUNTER_INCREMENTED generation=2' "$OUT/serial-provision.log"
grep -Fq 'TPM2_NV_COUNTER_READY generation=2' "$OUT/serial-reboot.log"
grep -Fq 'TPM2_NV_COUNTER_INCREMENTED generation=3' "$OUT/serial-reboot.log"
grep -Fq 'TPM2_NV_COUNTER_READY generation=3' "$OUT/serial-persistent.log"
grep -Fq 'TPM2_TIS_UNAVAILABLE' "$OUT/serial-absent.log"
grep -Fq 'ZENOVOS_UI_READY' "$OUT/serial-absent.log"
! grep -Eq 'KERNEL PANIC|DOUBLE FAULT|ASSERT' "$OUT"/serial-*.log

printf 'TPM2_NV_QEMU_OK tis=mmio locality=0 provision=explicit counter=1-2-3 reboot=persistent absent=compatible\n' \
  | tee "$OUT/summary.log"
