#!/usr/bin/env sh
set -eu

base_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image="$base_dir/ZenovOS-0.1.0-x86.img"

command -v qemu-system-i386 >/dev/null 2>&1 || {
  echo "qemu-system-i386 was not found. Install QEMU and try again." >&2
  exit 1
}

exec qemu-system-i386 \
  -drive "file=$image,format=raw,if=floppy" \
  -boot a \
  -m 32M
