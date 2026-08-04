#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -Eeuo pipefail

on_error() {
  local status=$?
  local source="${BASH_SOURCE[1]:-${BASH_SOURCE[0]}}"
  local line="${BASH_LINENO[0]:-0}"
  printf 'qemu-zenpkg-foreign: %s:%s: status=%s command=%q\n' \
    "$source" "$line" "$status" "$BASH_COMMAND" >&2
  exit "$status"
}
trap on_error ERR

QEMU="${QEMU:-qemu-system-i386}"
BOOT_IMAGE="${1:-build/zenov-os.img}"
DATA_IMAGE="${2:-build/zenov-data.img}"
OUT="${3:-build/qemu/zenpkg-foreign}"
PROMPT='zenov> '

mkdir -p "$OUT"
rm -f "$OUT/serial.log" "$OUT/monitor.log" "$OUT/qemu.stderr" "$OUT/runtime.img" "$OUT/summary.log"

wait_for_serial() {
  local file="$1"
  local text="$2"
  local timeout_tenths="${3:-900}"
  local i
  for ((i=0; i<timeout_tenths; ++i)); do
    [[ -f "$file" ]] && grep -Fq "$text" "$file" && return 0
    sleep 0.1
  done
  echo "qemu-zenpkg-foreign: missing serial marker: $text" >&2
  return 1
}

wait_for_serial_count() {
  local file="$1"
  local text="$2"
  local expected="$3"
  local timeout_tenths="${4:-900}"
  local i
  local count
  for ((i=0; i<timeout_tenths; ++i)); do
    count=0
    [[ -f "$file" ]] && count="$(grep -Fc "$text" "$file" || true)"
    (( count >= expected )) && return 0
    sleep 0.1
  done
  echo "qemu-zenpkg-foreign: missing serial count: $text expected=$expected actual=${count:-0}" >&2
  return 1
}

send_text() {
  local text="$1"
  local char
  local lower
  local i
  for ((i=0; i<${#text}; ++i)); do
    char="${text:i:1}"
    case "$char" in
      [a-z0-9]) echo "sendkey $char 10" ;;
      [A-Z]) lower="${char,,}"; echo "sendkey shift-$lower 10" ;;
      ' ') echo 'sendkey spc 10' ;;
      '.') echo 'sendkey dot 10' ;;
      '-') echo 'sendkey minus 10' ;;
      '_') echo 'sendkey shift-minus 10' ;;
      '/') echo 'sendkey slash 10' ;;
      *) echo "qemu-zenpkg-foreign: unsupported test key: $char" >&2; return 1 ;;
    esac
    sleep 0.01
  done
}

send_command() {
  send_text "$1"
  echo 'sendkey ret 10'
}

controller() {
  local serial="$1"
  wait_for_serial "$serial" 'ZENOVOS_BOOT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVFS_MOUNT_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENREPO_READY trust=verified packages=2' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENPKG_MANAGER_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" 'ZENOVOS_UI_READY' || { echo quit; return 1; }
  wait_for_serial "$serial" "$PROMPT" || { echo quit; return 1; }

  send_command 'pkg formats'
  wait_for_serial "$serial" 'ZENPKG_FORMATS_OK' || { echo quit; return 1; }

  send_command 'pkg compat status'
  wait_for_serial "$serial" 'ZENPKG_COMPAT_READY validators=5 runtimes=2 fail-closed=1 antivirus-unchanged=1' || { echo quit; return 1; }

  send_command 'pkg compat check /samples/linux-i386-hello.elf'
  wait_for_serial "$serial" 'ZENPKG_COMPAT_PREFLIGHT_OK format=elf structural=1 runtime=1 trust=0 verdict=runnable-sandbox' || { echo quit; return 1; }

  send_command 'pkg compat run-linux /samples/linux-i386-hello.elf'
  wait_for_serial "$serial" 'LINUX_I386_SYSCALL_ENOSYS number=5' || { echo quit; return 1; }
  wait_for_serial "$serial" 'LINUX_I386_SYSCALL_DENIED syscall=write reason=fd' || { echo quit; return 1; }
  wait_for_serial "$serial" 'LINUX_I386_COMPAT_WRITE_OK' || { echo quit; return 1; }
  wait_for_serial "$serial" 'LINUX_I386_COMPAT_EXIT code=0' || { echo quit; return 1; }

  send_command 'pkg compat check /samples/psx-r3000a-hello.exe'
  wait_for_serial "$serial" 'ZENPKG_COMPAT_PREFLIGHT_OK format=playstation-psx-exe structural=1 runtime=1 trust=0 verdict=runnable-sandbox' || { echo quit; return 1; }

  send_command 'pkg compat run-psx /samples/psx-r3000a-hello.exe'
  wait_for_serial_count "$serial" 'PSX_R3000A_HLE_PUTS_OK' 1 || { echo quit; return 1; }
  wait_for_serial_count "$serial" 'PSX_R3000A_EXIT code=0' 1 || { echo quit; return 1; }
  wait_for_serial_count "$serial" 'PSX_GUEST_RAM_WIPED pages=512 bytes=2097152' 1 || { echo quit; return 1; }

  send_command 'pkg compat run-psx /samples/psx-r3000a-hello.exe'
  wait_for_serial_count "$serial" 'PSX_R3000A_HLE_PUTS_OK' 2 || { echo quit; return 1; }
  wait_for_serial_count "$serial" 'PSX_R3000A_EXIT code=0' 2 || { echo quit; return 1; }
  wait_for_serial_count "$serial" 'PSX_GUEST_RAM_WIPED pages=512 bytes=2097152' 2 || { echo quit; return 1; }

  send_command 'pkg probe /data/packages/hello-native-0.1.0.zpk'
  wait_for_serial "$serial" 'ZENPKG_PROBE_OK format=zenpkg support=installable extension=0' || { echo quit; return 1; }

  send_command 'pkg install /data/packages/hello-native-0.1.0.zpk'
  wait_for_serial "$serial" 'ZENPKG_INSTALL_COMMIT_OK' || { echo quit; return 1; }

  send_command 'pkg run hello-native'
  wait_for_serial "$serial" 'ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0' || { echo quit; return 1; }
  wait_for_serial "$serial" 'HELLO_ZEX_0_1_1_OK' || { echo quit; return 1; }

  send_command 'fsck'
  wait_for_serial "$serial" 'ZENOVFS_FSCK_OK' || { echo quit; return 1; }
  sleep 0.2
  echo quit
}

[[ -f "$BOOT_IMAGE" && -f "$DATA_IMAGE" ]] || {
  echo 'qemu-zenpkg-foreign: boot and data images are required' >&2
  exit 2
}

cp "$DATA_IMAGE" "$OUT/runtime.img"
cmp "$DATA_IMAGE" "$OUT/runtime.img"

set +e
controller "$OUT/serial.log" | timeout 100s "$QEMU" \
  -drive "file=$BOOT_IMAGE,format=raw,if=floppy" \
  -drive "file=$OUT/runtime.img,format=raw,if=ide,index=0,media=disk" \
  -boot a -m 32M -machine pc,vmport=off -vga std -display none \
  -serial "file:$OUT/serial.log" -monitor stdio -no-reboot -no-shutdown \
  >"$OUT/monitor.log" 2>"$OUT/qemu.stderr"
status=$?
set -e

if [[ $status -ne 0 ]]; then
  echo "qemu-zenpkg-foreign: QEMU/controller failed with status $status" >&2
  cat "$OUT/monitor.log" >&2 || true
  cat "$OUT/qemu.stderr" >&2 || true
  cat "$OUT/serial.log" >&2 || true
  exit 1
fi

[[ ! -s "$OUT/qemu.stderr" ]] || {
  echo 'qemu-zenpkg-foreign: non-empty QEMU stderr' >&2
  cat "$OUT/qemu.stderr" >&2
  exit 1
}

for marker in \
  ZENPKG_FORMATS_OK \
  'ZENPKG_COMPAT_READY validators=5 runtimes=2 fail-closed=1 antivirus-unchanged=1' \
  'ZENPKG_COMPAT_PREFLIGHT_OK format=elf structural=1 runtime=1 trust=0 verdict=runnable-sandbox' \
  'LINUX_I386_SYSCALL_ENOSYS number=5' \
  'LINUX_I386_SYSCALL_DENIED syscall=write reason=fd' \
  LINUX_I386_COMPAT_WRITE_OK \
  'LINUX_I386_COMPAT_EXIT code=0' \
  'ZENPKG_COMPAT_PREFLIGHT_OK format=playstation-psx-exe structural=1 runtime=1 trust=0 verdict=runnable-sandbox' \
  PSX_R3000A_HLE_PUTS_OK \
  'PSX_R3000A_STOP reason=exited steps=8 output=23 memory-released=1' \
  'PSX_R3000A_EXIT code=0' \
  'PSX_GUEST_RAM_WIPED pages=512 bytes=2097152' \
  'ZENPKG_PROBE_OK format=zenpkg support=installable extension=0' \
  ZENPKG_INSTALL_COMMIT_OK \
  'ZENPKG_EXEC_ALLOWED name=hello-native version=0.1.0' \
  HELLO_ZEX_0_1_1_OK \
  ZENOVFS_FSCK_OK; do
  grep -Fq "$marker" "$OUT/serial.log" || {
    echo "qemu-zenpkg-foreign: missing final marker: $marker" >&2
    exit 1
  }
done

for marker in PSX_R3000A_HLE_PUTS_OK 'PSX_R3000A_EXIT code=0' 'PSX_GUEST_RAM_WIPED pages=512 bytes=2097152'; do
  [[ "$(grep -Fc "$marker" "$OUT/serial.log")" -eq 2 ]] || {
    echo "qemu-zenpkg-foreign: expected exactly two PS1 lifecycle markers: $marker" >&2
    exit 1
  }
done

if grep -Eq 'KERNEL PANIC|DOUBLE FAULT|ASSERT' "$OUT/serial.log"; then
  echo 'qemu-zenpkg-foreign: fatal guest marker detected' >&2
  exit 1
fi

printf 'ZENPKG_FOREIGN_QEMU_OK formats=2 preflight=linux-i386+psx linux-i386=write+exit+enosys+sandbox psx=r3000a+delay+hle-console+budget+wipe+reuse probe=zenpkg install=1 run=1 fsck=1\n' | tee "$OUT/summary.log"
