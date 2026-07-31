#!/usr/bin/env bash
set -euo pipefail

QEMU="${QEMU:-qemu-system-i386}"
ISO_IMAGE="${1:-build/ZenovOS-0.1.1-x86.iso}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu-iso}"

for tool in "$QEMU" xorriso cmp grep cp; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "qemu-iso-smoke: required tool not found: $tool" >&2
    exit 1
  }
done
[[ -f "$ISO_IMAGE" ]] || { echo "qemu-iso-smoke: missing ISO: $ISO_IMAGE" >&2; exit 1; }
[[ -f "$DATA_IMAGE" ]] || { echo "qemu-iso-smoke: missing data image: $DATA_IMAGE" >&2; exit 1; }

mkdir -p "$OUT"
SERIAL="$(cd "$OUT" && pwd)/serial.log"
STDERR="$(cd "$OUT" && pwd)/qemu.stderr"
RUNTIME_DATA="$(cd "$OUT" && pwd)/zenov-data-runtime.img"
rm -f "$SERIAL" "$STDERR" "$RUNTIME_DATA"
cp "$DATA_IMAGE" "$RUNTIME_DATA"

report="$(xorriso -indev "$ISO_IMAGE" -report_el_torito plain 2>/dev/null)"
grep -Fq 'El Torito catalog' <<<"$report"
grep -Eq 'BIOS|0x00' <<<"$report"
grep -Eiq 'floppy|fd|1\.44' <<<"$report"

"$QEMU" \
  -machine pc \
  -m 64M \
  -drive "file=$RUNTIME_DATA,format=raw,if=ide,index=0,media=disk" \
  -drive "file=$ISO_IMAGE,format=raw,if=ide,index=2,media=cdrom,readonly=on" \
  -boot order=d,strict=on \
  -serial "file:$SERIAL" \
  -monitor none \
  -display none \
  -no-reboot \
  -no-shutdown \
  2>"$STDERR" &
qemu_pid=$!

cleanup() {
  if kill -0 "$qemu_pid" 2>/dev/null; then
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

wait_for_marker() {
  local marker="$1"
  for _ in $(seq 1 600); do
    [[ -f "$SERIAL" ]] && grep -Fq "$marker" "$SERIAL" && return 0
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
      echo "qemu-iso-smoke: QEMU exited before marker: $marker" >&2
      cat "$STDERR" >&2 || true
      return 1
    fi
    sleep 0.1
  done
  echo "qemu-iso-smoke: timed out waiting for marker: $marker" >&2
  tail -200 "$SERIAL" >&2 || true
  cat "$STDERR" >&2 || true
  return 1
}

wait_for_marker ZENOVOS_BOOT_OK
wait_for_marker PMM_OK
wait_for_marker PAGING_OK
wait_for_marker ZENOVFS_MOUNT_OK
wait_for_marker PROCESS_ABI_0_1_1_OK
wait_for_marker GRAPHICAL_DESKTOP_READY
wait_for_marker ZENOVOS_UI_READY
wait_for_marker 'zenov> '

cleanup
trap - EXIT
cmp "$DATA_IMAGE" "$RUNTIME_DATA" >/dev/null 2>&1 || true
printf 'qemu-iso-smoke: OK iso=%s data=%s serial=%s\n' "$ISO_IMAGE" "$RUNTIME_DATA" "$SERIAL"
