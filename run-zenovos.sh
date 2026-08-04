#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO=""
MEMORY_MIB=128
ACCEL="auto"
HEADLESS=0
DRY_RUN=0
SERIAL_LOG="${ZENOVOS_SERIAL_LOG:-$ROOT/zenovos-serial.log}"

usage() {
  cat <<'EOF'
Usage: ./run-zenovos.sh [options]

Boot the self-contained ZenovOS Live ISO in QEMU. Hardware acceleration is
used when /dev/kvm is usable; otherwise the launcher falls back to TCG, which
works without AMD-V/VT-x.

Options:
  --iso PATH        ISO path (auto-detected when omitted)
  --memory MIB      Guest RAM in MiB, 64-512 (default: 128)
  --accel MODE      auto, kvm, or tcg (default: auto)
  --serial-log PATH COM1 diagnostic log (default: ./zenovos-serial.log)
  --headless        No graphical window; serial console on this terminal
  --dry-run         Validate inputs and print the exact QEMU command
  -h, --help        Show this help
EOF
}

fail() {
  printf 'run-zenovos: %s\n' "$*" >&2
  exit 1
}

while (($#)); do
  case "$1" in
    --iso)
      (($# >= 2)) || fail '--iso requires a path'
      ISO="$2"
      shift 2
      ;;
    --memory)
      (($# >= 2)) || fail '--memory requires a MiB value'
      MEMORY_MIB="$2"
      shift 2
      ;;
    --accel)
      (($# >= 2)) || fail '--accel requires auto, kvm, or tcg'
      ACCEL="$2"
      shift 2
      ;;
    --serial-log)
      (($# >= 2)) || fail '--serial-log requires a path'
      SERIAL_LOG="$2"
      shift 2
      ;;
    --headless)
      HEADLESS=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
done

[[ "$MEMORY_MIB" =~ ^[0-9]+$ ]] || fail '--memory must be an integer'
((MEMORY_MIB >= 64 && MEMORY_MIB <= 512)) || fail '--memory must be between 64 and 512 MiB'
case "$ACCEL" in auto|kvm|tcg) ;; *) fail '--accel must be auto, kvm, or tcg' ;; esac

if [[ -z "$ISO" ]]; then
  candidates=(
    "$PWD/ZenovOS-0.1.1-x86.iso"
    "$PWD/build/ZenovOS-0.1.1-x86.iso"
    "$ROOT/ZenovOS-0.1.1-x86.iso"
    "$ROOT/build/ZenovOS-0.1.1-x86.iso"
  )
  for candidate in "${candidates[@]}"; do
    if [[ -f "$candidate" ]]; then
      ISO="$candidate"
      break
    fi
  done
fi
[[ -n "$ISO" ]] || fail 'ISO not found; pass --iso /path/to/ZenovOS-0.1.1-x86.iso'
[[ -f "$ISO" && -r "$ISO" ]] || fail "ISO is not a readable file: $ISO"
[[ -s "$ISO" ]] || fail "ISO is empty: $ISO"
ISO="$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")"

QEMU="${QEMU:-}"
if [[ -n "$QEMU" ]]; then
  if [[ "$QEMU" == */* ]]; then
    [[ -x "$QEMU" ]] || fail "configured QEMU is not executable: $QEMU"
    QEMU="$(cd "$(dirname "$QEMU")" && pwd)/$(basename "$QEMU")"
  else
    command -v "$QEMU" >/dev/null 2>&1 || fail "configured QEMU command not found in PATH: $QEMU"
    QEMU="$(command -v "$QEMU")"
  fi
else
  for candidate in qemu-system-i386 qemu-system-x86_64; do
    if command -v "$candidate" >/dev/null 2>&1; then
      QEMU="$(command -v "$candidate")"
      break
    fi
  done
fi
[[ -n "$QEMU" && -x "$QEMU" ]] || fail 'QEMU not found; on CachyOS install package qemu-desktop'

qemu_supports_accel() {
  "$QEMU" -accel help 2>/dev/null | grep -Eq "(^|[[:space:]])$1($|[[:space:]])"
}

SELECTED_ACCEL="$ACCEL"
if [[ "$ACCEL" == auto ]]; then
  if [[ -c /dev/kvm && -r /dev/kvm && -w /dev/kvm ]] && qemu_supports_accel kvm; then
    SELECTED_ACCEL=kvm
  else
    SELECTED_ACCEL=tcg
  fi
elif [[ "$ACCEL" == kvm ]]; then
  [[ -c /dev/kvm && -r /dev/kvm && -w /dev/kvm ]] || fail 'KVM requested, but /dev/kvm is unavailable to this user'
  qemu_supports_accel kvm || fail 'this QEMU build does not provide KVM acceleration'
else
  qemu_supports_accel tcg || fail 'this QEMU build does not provide TCG acceleration'
fi

args=(
  -name ZenovOS
  -machine pc,vmport=off
  -accel "$SELECTED_ACCEL"
  -m "${MEMORY_MIB}M"
  -vga std
  -drive "file=$ISO,format=raw,if=ide,index=2,media=cdrom,readonly=on"
  -boot order=d,strict=on
  -monitor none
  -no-reboot
)

if ((HEADLESS)); then
  args+=( -display none -serial stdio )
else
  mkdir -p "$(dirname "$SERIAL_LOG")"
  : > "$SERIAL_LOG"
  args+=( -serial "file:$SERIAL_LOG" )
fi

printf 'run-zenovos: iso=%s memory=%sMiB accel=%s qemu=%s\n' \
  "$ISO" "$MEMORY_MIB" "$SELECTED_ACCEL" "$QEMU"
if ((HEADLESS)); then
  printf 'run-zenovos: display=headless serial=stdio\n'
else
  printf 'run-zenovos: display=window serial=%s\n' "$SERIAL_LOG"
fi

printf 'run-zenovos: command:'
printf ' %q' "$QEMU" "${args[@]}"
printf '\n'

((DRY_RUN)) && exit 0
exec "$QEMU" "${args[@]}"
