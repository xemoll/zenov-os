#!/usr/bin/env sh
set -eu

base_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
boot_image="$base_dir/ZenovOS-0.1.1-r2-x86.img"
data_image="$base_dir/ZenovOS-0.1.1-r2-data.img"

command -v qemu-system-i386 >/dev/null 2>&1 || {
  echo "qemu-system-i386 was not found. Install QEMU and try again." >&2
  exit 1
}

[ -f "$boot_image" ] || { echo "Missing boot image: $boot_image" >&2; exit 1; }
[ -f "$data_image" ] || { echo "Missing persistent data image: $data_image" >&2; exit 1; }

exec qemu-system-i386 \
  -drive "file=$boot_image,format=raw,if=floppy" \
  -drive "file=$data_image,format=raw,if=ide,index=0,media=disk" \
  -boot a \
  -m 32M
