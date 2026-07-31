#!/usr/bin/env bash
set -euo pipefail

BOOT_IMAGE="${1:-build/zenov-os.img}"
OUTPUT_ISO="${2:-build/ZenovOS-0.1.1-x86.iso}"
ISO_ROOT="${3:-build/iso-root}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="0.1.1"
VOLUME_ID="ZENOVOS_011"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1784160000}"
ISO_DATE="2026071600000000"

for tool in xorriso sha256sum cmp stat od touch; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "build-iso: required tool not found: $tool" >&2
    exit 1
  }
done
[[ "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]] || {
  echo "build-iso: SOURCE_DATE_EPOCH must be an integer" >&2
  exit 1
}

[[ -f "$BOOT_IMAGE" ]] || {
  echo "build-iso: boot image not found: $BOOT_IMAGE" >&2
  exit 1
}
[[ "$(stat -c%s "$BOOT_IMAGE")" -eq 1474560 ]] || {
  echo "build-iso: expected a 1,474,560-byte FAT12 boot image" >&2
  exit 1
}
[[ "$(od -An -tx1 -j510 -N2 "$BOOT_IMAGE" | tr -d ' \n')" == "55aa" ]] || {
  echo "build-iso: boot image is missing the 0x55aa BIOS signature" >&2
  exit 1
}

rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/BOOT" "$(dirname "$OUTPUT_ISO")"
cp "$BOOT_IMAGE" "$ISO_ROOT/BOOT/ZENOVOS.IMG"
cp "$ROOT/packaging/ISO-README.txt" "$ISO_ROOT/README.TXT"
printf '%s\n' "$VERSION" > "$ISO_ROOT/VERSION.TXT"

# SOURCE_DATE_EPOCH alone makes xorrisofs inherit source mtimes. Normalize every
# source node as well as the volume and El Torito catalog timestamps so builds
# remain byte-identical even when ISO_ROOT and OUTPUT_ISO use different paths.
TZ=UTC touch -d "@$SOURCE_DATE_EPOCH" \
  "$ISO_ROOT/BOOT/ZENOVOS.IMG" \
  "$ISO_ROOT/README.TXT" \
  "$ISO_ROOT/VERSION.TXT" \
  "$ISO_ROOT/BOOT" \
  "$ISO_ROOT"

# Keep the existing verified FAT12 loader as the only boot authority. El Torito
# floppy emulation presents the 1.44 MiB image as BIOS drive A: and therefore
# does not require GRUB, ISOLINUX, or a second stage with a different trust path.
SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" xorriso -as mkisofs \
  -quiet \
  -iso-level 1 \
  -V "$VOLUME_ID" \
  -A "ZenovOS $VERSION" \
  -p "ZenovOS Project" \
  --modification-date="$ISO_DATE" \
  --set_all_file_dates "$ISO_DATE" \
  -b BOOT/ZENOVOS.IMG \
  -c BOOT/BOOT.CAT \
  -o "$OUTPUT_ISO" \
  "$ISO_ROOT"

[[ -s "$OUTPUT_ISO" ]] || {
  echo "build-iso: ISO was not created: $OUTPUT_ISO" >&2
  exit 1
}
[[ $(( $(stat -c%s "$OUTPUT_ISO") % 2048 )) -eq 0 ]] || {
  echo "build-iso: ISO size is not aligned to 2048-byte sectors" >&2
  exit 1
}

report="$(xorriso -indev "$OUTPUT_ISO" -report_el_torito plain 2>/dev/null)"
grep -Fq 'El Torito catalog' <<<"$report"
grep -Eq 'BIOS|0x00' <<<"$report"
grep -Eiq 'floppy|fd|1\.44' <<<"$report"

extracted="$(mktemp)"
trap 'rm -f "$extracted"' EXIT
xorriso -osirrox on -indev "$OUTPUT_ISO" \
  -extract /BOOT/ZENOVOS.IMG "$extracted" >/dev/null 2>&1
cmp "$BOOT_IMAGE" "$extracted"

printf 'build-iso: OK version=%s volume=%s boot=%s iso=%s sha256=%s\n' \
  "$VERSION" "$VOLUME_ID" "$BOOT_IMAGE" "$OUTPUT_ISO" \
  "$(sha256sum "$OUTPUT_ISO" | cut -d' ' -f1)"
